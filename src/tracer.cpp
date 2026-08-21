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

#include "tracer.hpp"
#include "spirv.hpp"
#include "device.hpp"
#include "vulkan_helpers.hpp"
#include <math/vector.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace
{

    using VulkanHelpers::findMemoryType;

    //! Workgroup size, matching the [numthreads(8, 8, 1)] in trace.slang.
    constexpr uint32_t WORKGROUP_SIZE{8u};

    /*!
        Block size for the output-image arena.

        Two 1280x720 half-float images are about 7 MiB together, so at this size the common case is
        a single allocation and a resize to anything reasonable stays a single allocation. A larger
        window simply gets a block sized to fit — the arena treats this as a granularity rather than
        a ceiling.
    */
    constexpr vk::DeviceSize IMAGE_BLOCK_BYTES{16u * 1024u * 1024u};

    //! Block size for the buffer arena. Comfortably larger than every table this owns put together.
    constexpr vk::DeviceSize BUFFER_BLOCK_BYTES{4u * 1024u * 1024u};

    //! Block size for the host-visible arena behind the dynamic instance buffers. The protocol's
    //! whole creature cap is ~37 KB of records per frame in flight, so one block carries them all.
    constexpr vk::DeviceSize HOST_BLOCK_BYTES{256u * 1024u};

    /*!
        Linear radiance, not a picture.

        Emissive surfaces in the Grid are far brighter than one, and the reflections of them
        brighter still in places, so an eight-bit target would clip everything interesting before
        the tone curve ever saw it. Half float keeps the range that bloom and tone mapping need.
    */
    constexpr vk::Format OUTPUT_FORMAT{vk::Format::eR16G16B16A16Sfloat};

    /*!
        Push constants for trace.slang. The layout must match the TracePushConstants struct there
        exactly, which the static assertions below pin down.
    */
    struct TracePushConstants {
        MathLib::Vec4 camera_position{};
        MathLib::Vec4 top_left{};
        MathLib::Vec4 pixel_delta_u{};
        MathLib::Vec4 pixel_delta_v{};
        uint32_t resolution_x{0u};
        uint32_t resolution_y{0u};
        uint32_t max_bounces{0u};
        uint32_t instance_count{0u};
    };

    static_assert(sizeof(TracePushConstants) == 80u, "Push constants must match the Slang struct layout exactly.");
    static_assert(offsetof(TracePushConstants, top_left) == 16u, "top_left must sit at offset 16.");
    static_assert(offsetof(TracePushConstants, pixel_delta_u) == 32u, "pixel_delta_u must sit at offset 32.");
    static_assert(offsetof(TracePushConstants, pixel_delta_v) == 48u, "pixel_delta_v must sit at offset 48.");
    static_assert(offsetof(TracePushConstants, resolution_x) == 64u, "resolution must sit at offset 64.");

    // The shader declares each of these structures too, and reads the buffers as flat arrays of
    // them. A silent disagreement here is an out-of-bounds read on the GPU rather than a compile
    // error, so the sizes are pinned on this side as well.
    static_assert(sizeof(Material) == 32u, "grid_optics.slang declares Material as 32 bytes.");
    static_assert(sizeof(BvhLib::Node) == 32u, "grid_bvh.slang declares Node as 32 bytes.");
    static_assert(sizeof(BvhLib::Triangle) == 48u, "grid_bvh.slang declares Triangle as 48 bytes.");

    /*
        grid_bvh.slang sizes its traversal stack with a literal 30, because a shader array needs a
        compile-time bound and it cannot see this header.

        The failure mode if the two drift is worse than a crash, which is why it is worth an assert
        that reads oddly. Raise the host's cap and the builder happily produces deeper trees; the
        shader's stack stays at thirty and its `stack_size < MAX_DEPTH` guard then *silently drops*
        the far child of every node it cannot hold. Rays stop finding surfaces that are demonstrably
        there, no validation layer objects, and the picture merely looks a little wrong.
    */
    static_assert(BvhLib::MAX_DEPTH == 30u, "grid_bvh.slang sizes its traversal stack with a literal 30. Change both or neither.");

} // namespace

Tracer::Tracer(const Device& device, const World& world, const std::vector<Material>& materials, uint32_t frames_in_flight, const std::string& shader_path,
    LoggingLib::Logger& logger, const uint32_t dynamic_instance_capacity) :
    m_device(&device),
    m_world(&world),
    m_logger(&logger),
    m_frames_in_flight(frames_in_flight),
    m_buffer_arena(device, vk::MemoryPropertyFlagBits::eDeviceLocal, BUFFER_BLOCK_BYTES),
    m_dynamic_capacity(dynamic_instance_capacity),
    m_dynamic_counts(frames_in_flight, 0u),
    m_host_arena(device, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, HOST_BLOCK_BYTES),
    m_image_arena(device, vk::MemoryPropertyFlagBits::eDeviceLocal, IMAGE_BLOCK_BYTES)
{
    if (materials.empty()) {
        throw std::runtime_error{"The tracer needs at least one material."};
    }

    m_materials = VulkanHelpers::uploadStorageBuffer(*m_device, m_buffer_arena, materials.data(), materials.size() * sizeof(Material));

    if (m_dynamic_capacity > 0u) {
        const vk::DeviceSize instance_bytes{static_cast<vk::DeviceSize>(m_dynamic_capacity) * sizeof(BvhLib::InstanceRecord)};
        for (uint32_t frame{0u}; frame < m_frames_in_flight; ++frame) {
            vk::raii::Buffer buffer{m_device->get(),
                vk::BufferCreateInfo{.size = instance_bytes, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .sharingMode = vk::SharingMode::eExclusive}};
            void* const mapped{m_host_arena.bind(buffer)};

            // Defined contents before the first staging: a dispatch that somehow precedes it
            // traces zero-node records and misses, rather than traversing garbage.
            std::memset(mapped, 0, static_cast<size_t>(instance_bytes));

            m_dynamic_instances.emplace_back(std::move(buffer));
            m_dynamic_mapped.push_back(mapped);
        }

        m_logger->logInfo("Dynamic placements: " + std::to_string(m_dynamic_capacity) + " records per frame across " + std::to_string(m_frames_in_flight)
            + " frames in flight, " + std::to_string(static_cast<size_t>(instance_bytes) * m_frames_in_flight) + " bytes host-visible.");
    }

    m_logger->logInfo("Optical materials uploaded: " + std::to_string(materials.size()) + " entries, " + std::to_string(materials.size() * sizeof(Material))
        + " bytes of device-local storage.");

    const std::array<vk::DescriptorSetLayoutBinding, 5> bindings{
        vk::DescriptorSetLayoutBinding{
            .binding = 0u, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{
            .binding = 1u, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{
            .binding = 2u, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{
            .binding = 3u, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{
            .binding = 4u, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute}};

    m_set_layout = vk::raii::DescriptorSetLayout{
        m_device->get(), vk::DescriptorSetLayoutCreateInfo{.bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data()}};

    const std::array<vk::DescriptorPoolSize, 2> pool_sizes{vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageImage, .descriptorCount = m_frames_in_flight},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = 4u * m_frames_in_flight}};

    // eFreeDescriptorSet is required rather than optional: vk::raii::DescriptorSets frees its sets
    // when destroyed, and vkFreeDescriptorSets against a pool created without this flag is a
    // validation error at shutdown.
    m_descriptor_pool = vk::raii::DescriptorPool{m_device->get(),
        vk::DescriptorPoolCreateInfo{.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = m_frames_in_flight,
            .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
            .pPoolSizes = pool_sizes.data()}};

    const std::vector<vk::DescriptorSetLayout> layouts(m_frames_in_flight, *m_set_layout);
    m_descriptor_sets = vk::raii::DescriptorSets{
        m_device->get(), vk::DescriptorSetAllocateInfo{.descriptorPool = *m_descriptor_pool, .descriptorSetCount = m_frames_in_flight, .pSetLayouts = layouts.data()}};

    const vk::PushConstantRange push_range{.stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0u, .size = sizeof(TracePushConstants)};
    m_pipeline_layout = vk::raii::PipelineLayout{m_device->get(),
        vk::PipelineLayoutCreateInfo{.setLayoutCount = 1u, .pSetLayouts = &*m_set_layout, .pushConstantRangeCount = 1u, .pPushConstantRanges = &push_range}};

    const std::vector<uint32_t> code{SpirvLib::read(shader_path)};
    const vk::raii::ShaderModule module{m_device->get(), vk::ShaderModuleCreateInfo{.codeSize = code.size() * sizeof(uint32_t), .pCode = code.data()}};

    m_pipeline = vk::raii::Pipeline{m_device->get(), nullptr,
        vk::ComputePipelineCreateInfo{.stage = vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eCompute, .module = *module, .pName = "traceMain"},
            .layout = *m_pipeline_layout}};
}

void Tracer::createOutputImages()
{
    // Views first, then images, then the memory they were bound to. The order is the reverse of
    // creation and it is not cosmetic: freeing a block an image is still bound to is undefined
    // behaviour, and this is the one place in the class where that could happen.
    m_output_views.clear();
    m_output_images.clear();

    m_image_arena.reset();

    for (uint32_t frame{0u}; frame < m_frames_in_flight; ++frame) {
        vk::raii::Image image{m_device->get(),
            vk::ImageCreateInfo{.imageType = vk::ImageType::e2D,
                .format = OUTPUT_FORMAT,
                .extent = vk::Extent3D{m_extent.width, m_extent.height, 1u},
                .mipLevels = 1u,
                .arrayLayers = 1u,
                .samples = vk::SampleCountFlagBits::e1,
                .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eStorage,
                .sharingMode = vk::SharingMode::eExclusive,
                .initialLayout = vk::ImageLayout::eUndefined}};

        m_image_arena.bind(image);

        vk::raii::ImageView view{m_device->get(),
            vk::ImageViewCreateInfo{.image = *image,
                .viewType = vk::ImageViewType::e2D,
                .format = OUTPUT_FORMAT,
                .subresourceRange = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}}};

        m_output_images.emplace_back(std::move(image));
        m_output_views.emplace_back(std::move(view));
    }
}

void Tracer::writeDescriptorSets()
{
    for (uint32_t frame{0u}; frame < m_frames_in_flight; ++frame) {
        const vk::DescriptorImageInfo image_info{.imageView = *m_output_views[frame], .imageLayout = vk::ImageLayout::eGeneral};

        const vk::DescriptorBufferInfo nodes_info{.buffer = m_world->nodes(), .offset = 0u, .range = vk::WholeSize};
        const vk::DescriptorBufferInfo triangles_info{.buffer = m_world->triangles(), .offset = 0u, .range = vk::WholeSize};
        const vk::DescriptorBufferInfo materials_info{.buffer = *m_materials.buffer, .offset = 0u, .range = vk::WholeSize};

        // The live view's slot traces its own moving placements; every other mode traces the
        // world's static upload, byte for byte the buffer the reference digest has always seen.
        const vk::DescriptorBufferInfo instances_info{
            .buffer = hasDynamicInstances() ? *m_dynamic_instances[frame] : m_world->instances(), .offset = 0u, .range = vk::WholeSize};

        const std::array<vk::WriteDescriptorSet, 5> writes{vk::WriteDescriptorSet{.dstSet = *m_descriptor_sets[frame],
                                                               .dstBinding = 0u,
                                                               .descriptorCount = 1u,
                                                               .descriptorType = vk::DescriptorType::eStorageImage,
                                                               .pImageInfo = &image_info},
            vk::WriteDescriptorSet{.dstSet = *m_descriptor_sets[frame],
                .dstBinding = 1u,
                .descriptorCount = 1u,
                .descriptorType = vk::DescriptorType::eStorageBuffer,
                .pBufferInfo = &nodes_info},
            vk::WriteDescriptorSet{.dstSet = *m_descriptor_sets[frame],
                .dstBinding = 2u,
                .descriptorCount = 1u,
                .descriptorType = vk::DescriptorType::eStorageBuffer,
                .pBufferInfo = &triangles_info},
            vk::WriteDescriptorSet{.dstSet = *m_descriptor_sets[frame],
                .dstBinding = 3u,
                .descriptorCount = 1u,
                .descriptorType = vk::DescriptorType::eStorageBuffer,
                .pBufferInfo = &materials_info},
            vk::WriteDescriptorSet{.dstSet = *m_descriptor_sets[frame],
                .dstBinding = 4u,
                .descriptorCount = 1u,
                .descriptorType = vk::DescriptorType::eStorageBuffer,
                .pBufferInfo = &instances_info}};

        m_device->get().updateDescriptorSets(writes, {});
    }
}

void Tracer::resize(vk::Extent2D extent)
{
    /*
        A zero extent is a minimised window, not an error. Swapchain::build already returns early on
        one, and the frame loop skips rendering while it lasts — but startup resizes unconditionally,
        so without this a program launched into a minimised window would reach vkCreateImage with a
        zero extent and VUID-VkImageCreateInfo-extent-00944. Guarding here covers every caller rather
        than asking each to remember.
    */
    if ((extent.width == 0u) || (extent.height == 0u)) {
        m_extent = vk::Extent2D{0u, 0u};
        return;
    }

    m_extent = extent;
    createOutputImages();
    writeDescriptorSets();
}

void Tracer::stageInstances(const uint32_t frame_slot, const std::vector<BvhLib::InstanceRecord>& records)
{
    if (!hasDynamicInstances()) {
        throw std::runtime_error{"This tracer renders static placements: it was built with no dynamic instance capacity."};
    }
    if (records.size() > m_dynamic_capacity) {
        throw std::runtime_error{"More placements than this tracer was built for: " + std::to_string(records.size()) + " of " + std::to_string(m_dynamic_capacity) + "."};
    }

    if (!records.empty()) {
        std::memcpy(m_dynamic_mapped[frame_slot], records.data(), records.size() * sizeof(BvhLib::InstanceRecord));
    }
    m_dynamic_counts[frame_slot] = static_cast<uint32_t>(records.size());
}

void Tracer::record(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot, const Camera& camera, uint32_t max_bounces) const
{
    // Undefined rather than the previous layout: every pixel is written unconditionally, so there
    // is nothing in the old contents worth preserving and discarding them is free.
    const vk::ImageMemoryBarrier2 to_general{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *m_output_images[frame_slot],
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}};
    command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &to_general});

    command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *m_pipeline);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_pipeline_layout, 0u, {*m_descriptor_sets[frame_slot]}, {});

    /*
        The ray basis, computed once per frame on the host.

        A direction per pixel is an affine function of the pixel coordinate, so the shader needs
        only the direction through the first pixel and the step between neighbours. That keeps the
        per-pixel work to two multiply-adds and avoids putting a matrix inverse in the inner loop.
    */
    const MathLib::Vec3 forward{camera.orientation().rotate(MathLib::Vec3{0.0f, 0.0f, -1.0f})};
    const MathLib::Vec3 right{camera.orientation().rotate(MathLib::Vec3{1.0f, 0.0f, 0.0f})};
    const MathLib::Vec3 up{camera.orientation().rotate(MathLib::Vec3{0.0f, 1.0f, 0.0f})};

    const float width{static_cast<float>(m_extent.width)};
    const float height{static_cast<float>(m_extent.height)};
    const float aspect{width / height};

    const float viewport_height{2.0f * std::tan(camera.fovY() * 0.5f)};
    const float viewport_width{viewport_height * aspect};

    const MathLib::Vec3 delta_u{right * (viewport_width / width)};
    const MathLib::Vec3 delta_v{up * (-viewport_height / height)};

    // Aim at pixel centres rather than corners, which is half a pixel in from the top-left edge.
    const MathLib::Vec3 top_left_corner{forward - (right * (viewport_width * 0.5f)) + (up * (viewport_height * 0.5f))};
    const MathLib::Vec3 top_left{top_left_corner + ((delta_u + delta_v) * 0.5f)};

    const TracePushConstants push{.camera_position = MathLib::Vec4{camera.position().x, camera.position().y, camera.position().z, 0.0f},
        .top_left = MathLib::Vec4{top_left.x, top_left.y, top_left.z, 0.0f},
        .pixel_delta_u = MathLib::Vec4{delta_u.x, delta_u.y, delta_u.z, 0.0f},
        .pixel_delta_v = MathLib::Vec4{delta_v.x, delta_v.y, delta_v.z, 0.0f},
        .resolution_x = m_extent.width,
        .resolution_y = m_extent.height,
        .max_bounces = max_bounces,
        .instance_count = hasDynamicInstances() ? m_dynamic_counts[frame_slot] : m_world->instanceCount()};

    command_buffer.pushConstants<TracePushConstants>(*m_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0u, {push});

    const uint32_t groups_x{(m_extent.width + WORKGROUP_SIZE - 1u) / WORKGROUP_SIZE};
    const uint32_t groups_y{(m_extent.height + WORKGROUP_SIZE - 1u) / WORKGROUP_SIZE};
    command_buffer.dispatch(groups_x, groups_y, 1u);

    // Left in eGeneral: the post-processing stage reads this as a storage image rather than
    // copying it, so there is no transfer layout to move into.
    const vk::ImageMemoryBarrier2 to_readable{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *m_output_images[frame_slot],
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}};
    command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &to_readable});
}
