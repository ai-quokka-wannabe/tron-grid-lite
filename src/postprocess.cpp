/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#include "postprocess.hpp"
#include "device.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace
{

    //! Workgroup size, matching [numthreads(8, 8, 1)] in both post-processing shaders.
    constexpr uint32_t WORKGROUP_SIZE{8u};

    //! Every bloom mip is linear half float, as the shaders' declared image formats require.
    constexpr vk::Format BLOOM_FORMAT{vk::Format::eR16G16B16A16Sfloat};

    //! The final picture. UNORM rather than sRGB, because the shader encodes the transfer function.
    constexpr vk::Format OUTPUT_FORMAT{vk::Format::eR8G8B8A8Unorm};

    //! Smallest mip worth having: below this the pyramid buys blur radius at no useful resolution.
    constexpr uint32_t MIN_BLOOM_EXTENT{8u};

    //! Hard ceiling on pyramid depth, so a large window cannot allocate an unbounded chain.
    constexpr uint32_t MAX_BLOOM_MIPS{6u};

    //! Push constants for bloom_downsample.slang.
    struct BloomPushConstants {
        float threshold{0.0f};
    };

    //! Push constants for postprocess.slang.
    struct PostProcessPushConstants {
        float bloom_strength{0.0f};
        float vignette_strength{0.0f};
        float exposure{1.0f};
    };

    static_assert(sizeof(BloomPushConstants) == 4u, "bloom_downsample.slang declares a 4-byte push constant block.");
    static_assert(sizeof(PostProcessPushConstants) == 12u, "postprocess.slang declares a 12-byte push constant block.");

    //! Finds a memory type satisfying both the resource's requirements and the requested properties.
    [[nodiscard]] uint32_t findMemoryType(const vk::raii::PhysicalDevice& physical_device, uint32_t type_bits, vk::MemoryPropertyFlags required)
    {
        const vk::PhysicalDeviceMemoryProperties properties{physical_device.getMemoryProperties()};
        for (uint32_t index{0u}; index < properties.memoryTypeCount; ++index) {
            if (((type_bits & (1u << index)) != 0u) && ((properties.memoryTypes[index].propertyFlags & required) == required)) {
                return index;
            }
        }
        throw std::runtime_error{"No memory type satisfies the requested properties."};
    }

    //! First word of every SPIR-V module, per the specification.
    constexpr uint32_t SPIRV_MAGIC{0x07230203u};

    //! Reads a compiled SPIR-V module from disk.
    [[nodiscard]] std::vector<uint32_t> readSpirv(const std::string& path)
    {
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file.is_open()) {
            throw std::runtime_error{"Failed to open SPIR-V module: " + path};
        }

        const std::streamsize size_bytes{file.tellg()};
        if ((size_bytes <= 0) || ((size_bytes % 4) != 0)) {
            throw std::runtime_error{"SPIR-V module has an invalid size: " + path};
        }

        std::vector<uint32_t> words(static_cast<size_t>(size_bytes) / 4u);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(words.data()), size_bytes);

        /*
            The read has to be checked. std::vector value-initialises, so a short read leaves a
            silently zero-filled tail while the full size is still reported to vkCreateShaderModule.
            A release build has no validation layer to reject the result, so the driver's SPIR-V
            parser consumes the zeros — a hang or a crash instead of the clean error this function
            is otherwise built to produce.
        */
        if (file.gcount() != size_bytes) {
            throw std::runtime_error{"Truncated SPIR-V module: " + path};
        }

        // The magic number catches the likelier mistake of pointing at the wrong file entirely.
        if (words.front() != SPIRV_MAGIC) {
            throw std::runtime_error{"Not a SPIR-V module: " + path};
        }

        return words;
    }

    //! Builds one compute pipeline from a module and an entry point name.
    [[nodiscard]] vk::raii::Pipeline makeComputePipeline(const vk::raii::Device& device, const vk::raii::ShaderModule& module, const char* entry_point,
        const vk::raii::PipelineLayout& layout)
    {
        return vk::raii::Pipeline{device, nullptr,
            vk::ComputePipelineCreateInfo{
                .stage = vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eCompute, .module = *module, .pName = entry_point}, .layout = *layout}};
    }

    //! Returns the number of workgroups needed to cover an extent.
    [[nodiscard]] uint32_t groupsFor(uint32_t pixels)
    {
        return (pixels + WORKGROUP_SIZE - 1u) / WORKGROUP_SIZE;
    }

    //! A descriptor set layout of `count` consecutive storage image bindings, all compute stage.
    [[nodiscard]] vk::raii::DescriptorSetLayout makeStorageImageLayout(const vk::raii::Device& device, uint32_t count)
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings(count);
        for (uint32_t index{0u}; index < count; ++index) {
            bindings[index] = vk::DescriptorSetLayoutBinding{
                .binding = index, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute};
        }
        return vk::raii::DescriptorSetLayout{device, vk::DescriptorSetLayoutCreateInfo{.bindingCount = count, .pBindings = bindings.data()}};
    }

} // namespace

PostProcess::PostProcess(const Device& device, uint32_t frames_in_flight, const std::string& bloom_shader_path, const std::string& postprocess_shader_path,
    LoggingLib::Logger& logger) :
    m_device(&device),
    m_logger(&logger),
    m_frames_in_flight(frames_in_flight)
{
    m_bloom_layout = makeStorageImageLayout(m_device->get(), 3u);
    m_postprocess_layout = makeStorageImageLayout(m_device->get(), 3u);

    /*
        The pyramid depth is not known until the first resize, so the pool is sized for the deepest
        chain the limits allow. Descriptor sets are cheap; reallocating a pool mid-frame is not.
    */
    const uint32_t max_bloom_sets_per_frame{(2u * MAX_BLOOM_MIPS) - 1u};
    const uint32_t max_sets{m_frames_in_flight * (max_bloom_sets_per_frame + 1u)};

    const vk::DescriptorPoolSize pool_size{.type = vk::DescriptorType::eStorageImage, .descriptorCount = 3u * max_sets};

    // eFreeDescriptorSet because vk::raii::DescriptorSets frees its sets when destroyed, which a
    // pool created without the flag rejects.
    m_descriptor_pool = vk::raii::DescriptorPool{m_device->get(),
        vk::DescriptorPoolCreateInfo{.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, .maxSets = max_sets, .poolSizeCount = 1u, .pPoolSizes = &pool_size}};

    const vk::PushConstantRange bloom_range{.stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0u, .size = sizeof(BloomPushConstants)};
    m_bloom_pipeline_layout = vk::raii::PipelineLayout{m_device->get(),
        vk::PipelineLayoutCreateInfo{.setLayoutCount = 1u, .pSetLayouts = &*m_bloom_layout, .pushConstantRangeCount = 1u, .pPushConstantRanges = &bloom_range}};

    const vk::PushConstantRange postprocess_range{.stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0u, .size = sizeof(PostProcessPushConstants)};
    m_postprocess_pipeline_layout = vk::raii::PipelineLayout{m_device->get(),
        vk::PipelineLayoutCreateInfo{
            .setLayoutCount = 1u, .pSetLayouts = &*m_postprocess_layout, .pushConstantRangeCount = 1u, .pPushConstantRanges = &postprocess_range}};

    const std::vector<uint32_t> bloom_code{readSpirv(bloom_shader_path)};
    const vk::raii::ShaderModule bloom_module{m_device->get(), vk::ShaderModuleCreateInfo{.codeSize = bloom_code.size() * sizeof(uint32_t), .pCode = bloom_code.data()}};

    m_extract_pipeline = makeComputePipeline(m_device->get(), bloom_module, "bloomExtractMain", m_bloom_pipeline_layout);
    m_downsample_pipeline = makeComputePipeline(m_device->get(), bloom_module, "bloomDownsampleMain", m_bloom_pipeline_layout);
    m_upsample_pipeline = makeComputePipeline(m_device->get(), bloom_module, "bloomUpsampleMain", m_bloom_pipeline_layout);

    const std::vector<uint32_t> postprocess_code{readSpirv(postprocess_shader_path)};
    const vk::raii::ShaderModule postprocess_module{
        m_device->get(), vk::ShaderModuleCreateInfo{.codeSize = postprocess_code.size() * sizeof(uint32_t), .pCode = postprocess_code.data()}};

    m_postprocess_pipeline = makeComputePipeline(m_device->get(), postprocess_module, "postprocessMain", m_postprocess_pipeline_layout);
}

PostProcess::OwnedImage PostProcess::createImage(vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage) const
{
    OwnedImage owned{};

    owned.image = vk::raii::Image{m_device->get(),
        vk::ImageCreateInfo{.imageType = vk::ImageType::e2D,
            .format = format,
            .extent = vk::Extent3D{extent.width, extent.height, 1u},
            .mipLevels = 1u,
            .arrayLayers = 1u,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined}};

    const vk::MemoryRequirements requirements{owned.image.getMemoryRequirements()};
    owned.memory = vk::raii::DeviceMemory{m_device->get(),
        vk::MemoryAllocateInfo{.allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(m_device->physicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)}};
    owned.image.bindMemory(*owned.memory, 0u);

    owned.view = vk::raii::ImageView{m_device->get(),
        vk::ImageViewCreateInfo{.image = *owned.image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}}};

    return owned;
}

void PostProcess::computeBloomExtents()
{
    m_bloom_extents.clear();

    /*
        Mip 0 is half the output, which is where the extract pass writes. Each level halves again.

        Rounded up rather than truncated. At an odd width the truncated mip is one texel short, and
        the last column of the source is then gathered by no thread at all — a neon tube whose only
        bright pixels sit in that column contributes no bloom. The window is resizable, so odd
        extents are reachable, and the truncation repeats at every level.
    */
    vk::Extent2D size{std::max((m_extent.width + 1u) / 2u, 1u), std::max((m_extent.height + 1u) / 2u, 1u)};

    while (m_bloom_extents.size() < MAX_BLOOM_MIPS) {
        m_bloom_extents.push_back(size);

        const uint32_t next_width{(size.width + 1u) / 2u};
        const uint32_t next_height{(size.height + 1u) / 2u};
        if ((next_width < MIN_BLOOM_EXTENT) || (next_height < MIN_BLOOM_EXTENT)) {
            break;
        }
        size = vk::Extent2D{next_width, next_height};
    }
}

void PostProcess::resize(vk::Extent2D extent, const std::vector<vk::ImageView>& hdr_views)
{
    if (hdr_views.size() != m_frames_in_flight) {
        throw std::runtime_error{"The post-process stage needs one HDR view per frame in flight."};
    }

    m_extent = extent;
    computeBloomExtents();

    // Destroy before allocating, so a resize does not hold two full sets of images at once.
    m_bloom_sets.clear();
    m_postprocess_sets.clear();
    m_all_sets.clear();
    m_bloom_mips.clear();
    m_output_images.clear();

    m_bloom_mips.resize(m_frames_in_flight);
    for (uint32_t frame{0u}; frame < m_frames_in_flight; ++frame) {
        for (const vk::Extent2D& mip_extent : m_bloom_extents) {
            // eTransferDst so that mip 0 can be cleared when bloom is switched off. The tone
            // mapping pass reads it unconditionally, so it must never hold undefined memory.
            m_bloom_mips[frame].emplace_back(createImage(mip_extent, BLOOM_FORMAT, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst));
        }
        m_output_images.emplace_back(createImage(m_extent, OUTPUT_FORMAT, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc));
    }

    writeDescriptorSets(hdr_views);

    m_logger->logInfo("Post-processing allocated: " + std::to_string(m_bloom_extents.size()) + " bloom mips from " + std::to_string(m_bloom_extents.front().width) + "x"
        + std::to_string(m_bloom_extents.front().height) + " down to " + std::to_string(m_bloom_extents.back().width) + "x"
        + std::to_string(m_bloom_extents.back().height) + ".");
}

void PostProcess::writeDescriptorSets(const std::vector<vk::ImageView>& hdr_views)
{
    const uint32_t mip_count{static_cast<uint32_t>(m_bloom_extents.size())};

    // Extract, then one downsample per step down, then one upsample per step back up.
    const uint32_t bloom_sets_per_frame{(2u * mip_count) - 1u};

    std::vector<vk::DescriptorSetLayout> layouts;
    for (uint32_t frame{0u}; frame < m_frames_in_flight; ++frame) {
        for (uint32_t index{0u}; index < bloom_sets_per_frame; ++index) {
            layouts.push_back(*m_bloom_layout);
        }
        layouts.push_back(*m_postprocess_layout);
    }

    m_all_sets = vk::raii::DescriptorSets{m_device->get(),
        vk::DescriptorSetAllocateInfo{.descriptorPool = *m_descriptor_pool, .descriptorSetCount = static_cast<uint32_t>(layouts.size()), .pSetLayouts = layouts.data()}};

    m_bloom_sets.assign(m_frames_in_flight, {});
    m_postprocess_sets.assign(m_frames_in_flight, vk::DescriptorSet{});

    /*
        Every write is collected before any of them is submitted, because vk::DescriptorImageInfo
        objects must outlive the updateDescriptorSets call that points at them. Building the info
        list in a loop and updating as we go would leave dangling pointers.
    */
    std::vector<vk::DescriptorImageInfo> infos;
    infos.reserve(layouts.size() * 3u);
    std::vector<vk::WriteDescriptorSet> writes;
    writes.reserve(layouts.size() * 3u);

    const auto addSet = [&](vk::DescriptorSet set, const std::array<vk::ImageView, 3>& views) {
        const size_t first{infos.size()};
        for (const vk::ImageView view : views) {
            infos.push_back(vk::DescriptorImageInfo{.imageView = view, .imageLayout = vk::ImageLayout::eGeneral});
        }
        for (uint32_t binding{0u}; binding < 3u; ++binding) {
            writes.push_back(vk::WriteDescriptorSet{
                .dstSet = set, .dstBinding = binding, .descriptorCount = 1u, .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &infos[first + binding]});
        }
    };

    uint32_t next_set{0u};
    for (uint32_t frame{0u}; frame < m_frames_in_flight; ++frame) {
        const vk::ImageView hdr{hdr_views[frame]};
        const std::vector<OwnedImage>& mips{m_bloom_mips[frame]};

        // Extract: reads the full-resolution HDR image, writes mip 0.
        addSet(*m_all_sets[next_set], {hdr, *mips[0].view, *mips[0].view});
        m_bloom_sets[frame].push_back(*m_all_sets[next_set]);
        ++next_set;

        // Downsample: mip i becomes mip i + 1, walking down the pyramid.
        for (uint32_t mip{0u}; (mip + 1u) < mip_count; ++mip) {
            addSet(*m_all_sets[next_set], {hdr, *mips[mip].view, *mips[mip + 1u].view});
            m_bloom_sets[frame].push_back(*m_all_sets[next_set]);
            ++next_set;
        }

        // Upsample: mip i + 1 is added into mip i, walking back up.
        for (uint32_t mip{mip_count - 1u}; mip > 0u; --mip) {
            addSet(*m_all_sets[next_set], {hdr, *mips[mip].view, *mips[mip - 1u].view});
            m_bloom_sets[frame].push_back(*m_all_sets[next_set]);
            ++next_set;
        }

        // Tone mapping: HDR plus the finished mip 0, into the output image.
        addSet(*m_all_sets[next_set], {hdr, *m_output_images[frame].view, *mips[0].view});
        m_postprocess_sets[frame] = *m_all_sets[next_set];
        ++next_set;
    }

    m_device->get().updateDescriptorSets(writes, {});
}

void PostProcess::barrierBetweenMips(const vk::raii::CommandBuffer& command_buffer, vk::Image image)
{
    // The next dispatch reads what this one wrote, and both are compute shader storage access.
    const vk::ImageMemoryBarrier2 barrier{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}};
    command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &barrier});
}

void PostProcess::record(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot, float bloom_threshold, float bloom_strength, float vignette_strength,
    float exposure) const
{
    const uint32_t mip_count{static_cast<uint32_t>(m_bloom_extents.size())};
    const std::vector<OwnedImage>& mips{m_bloom_mips[frame_slot]};
    const std::vector<vk::DescriptorSet>& sets{m_bloom_sets[frame_slot]};

    /*
        Every bloom mip starts each frame in an undefined layout. Discarding the previous frame's
        contents is correct as well as free: the extract pass overwrites mip 0 completely, and the
        upsample passes add into mips that the downsample passes have already rewritten this frame.
    */
    std::vector<vk::ImageMemoryBarrier2> initial_barriers;
    initial_barriers.reserve(mip_count + 1u);
    for (uint32_t mip{0u}; mip < mip_count; ++mip) {
        initial_barriers.push_back(vk::ImageMemoryBarrier2{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = *mips[mip].image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}});
    }
    initial_barriers.push_back(vk::ImageMemoryBarrier2{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *m_output_images[frame_slot].image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}});

    command_buffer.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<uint32_t>(initial_barriers.size()), .pImageMemoryBarriers = initial_barriers.data()});

    const bool bloom_enabled{bloom_strength > 0.0f};

    if (bloom_enabled) {
        const BloomPushConstants bloom_push{.threshold = bloom_threshold};
        uint32_t set_index{0u};

        // Extract into mip 0 at half resolution.
        command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *m_extract_pipeline);
        command_buffer.pushConstants<BloomPushConstants>(*m_bloom_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0u, {bloom_push});
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_bloom_pipeline_layout, 0u, {sets[set_index]}, {});
        command_buffer.dispatch(groupsFor(m_bloom_extents[0].width), groupsFor(m_bloom_extents[0].height), 1u);
        ++set_index;

        // Down the pyramid.
        command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *m_downsample_pipeline);
        for (uint32_t mip{0u}; (mip + 1u) < mip_count; ++mip) {
            barrierBetweenMips(command_buffer, *mips[mip].image);
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_bloom_pipeline_layout, 0u, {sets[set_index]}, {});
            command_buffer.dispatch(groupsFor(m_bloom_extents[mip + 1u].width), groupsFor(m_bloom_extents[mip + 1u].height), 1u);
            ++set_index;
        }

        // And back up, each level adding into the one below it.
        command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *m_upsample_pipeline);
        for (uint32_t mip{mip_count - 1u}; mip > 0u; --mip) {
            barrierBetweenMips(command_buffer, *mips[mip].image);
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_bloom_pipeline_layout, 0u, {sets[set_index]}, {});
            command_buffer.dispatch(groupsFor(m_bloom_extents[mip - 1u].width), groupsFor(m_bloom_extents[mip - 1u].height), 1u);
            ++set_index;
        }

        // Mip 0's final contents must be visible to the tone mapping pass.
        barrierBetweenMips(command_buffer, *mips[0].image);
    } else {
        /*
            The tone mapping shader reads the bloom image unconditionally and scales it by the
            strength, so a strength of zero does not excuse leaving the image undefined: multiplying
            an uninitialised half float by zero gives zero only if it was not a NaN. Clearing costs
            one small transfer and removes the question.
        */
        const vk::ImageMemoryBarrier2 to_transfer_dst{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eClear,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = *mips[0].image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &to_transfer_dst});

        const vk::ClearColorValue black{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
        const vk::ImageSubresourceRange whole_image{vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u};
        command_buffer.clearColorImage(*mips[0].image, vk::ImageLayout::eTransferDstOptimal, black, {whole_image});

        const vk::ImageMemoryBarrier2 back_to_general{.srcStageMask = vk::PipelineStageFlagBits2::eClear,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = *mips[0].image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &back_to_general});
    }

    const PostProcessPushConstants postprocess_push{.bloom_strength = bloom_strength, .vignette_strength = vignette_strength, .exposure = exposure};

    command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *m_postprocess_pipeline);
    command_buffer.pushConstants<PostProcessPushConstants>(*m_postprocess_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0u, {postprocess_push});
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_postprocess_pipeline_layout, 0u, {m_postprocess_sets[frame_slot]}, {});
    command_buffer.dispatch(groupsFor(m_extent.width), groupsFor(m_extent.height), 1u);

    /*
        eAllTransfer rather than eBlit: this stage hands over an image in eTransferSrcOptimal and
        has no business assuming how the consumer reads it. The interactive path blits it to the
        swapchain, the recording path copies it into a buffer, and a copy is a different pipeline
        stage — naming only the blit leaves the copy unordered against this transition, which
        synchronisation validation reports as a read-after-write hazard.
    */
    const vk::ImageMemoryBarrier2 to_transfer_src{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *m_output_images[frame_slot].image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}};
    command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &to_transfer_src});
}
