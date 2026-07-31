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
#include "device.hpp"
#include <math/vector.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace
{

    //! Workgroup size, matching the [numthreads(8, 8, 1)] in trace.slang.
    constexpr uint32_t WORKGROUP_SIZE{8u};

    //! Format of the traced picture. UNORM rather than sRGB because the shader encodes the transfer function itself.
    constexpr vk::Format OUTPUT_FORMAT{vk::Format::eR8G8B8A8Unorm};

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
        uint32_t node_count{0u};
        float exposure{1.0f};
        float padding[3]{0.0f, 0.0f, 0.0f};
    };

    static_assert(sizeof(TracePushConstants) == 96u, "Push constants must match the Slang struct layout exactly.");
    static_assert(offsetof(TracePushConstants, top_left) == 16u, "top_left must sit at offset 16.");
    static_assert(offsetof(TracePushConstants, pixel_delta_u) == 32u, "pixel_delta_u must sit at offset 32.");
    static_assert(offsetof(TracePushConstants, pixel_delta_v) == 48u, "pixel_delta_v must sit at offset 48.");
    static_assert(offsetof(TracePushConstants, resolution_x) == 64u, "resolution must sit at offset 64.");
    static_assert(offsetof(TracePushConstants, exposure) == 80u, "exposure must sit at offset 80.");

    // The shader declares each of these structures too, and reads the buffers as flat arrays of
    // them. A silent disagreement here is an out-of-bounds read on the GPU rather than a compile
    // error, so the sizes are pinned on this side as well.
    static_assert(sizeof(Material) == 32u, "trace.slang declares Material as 32 bytes.");
    static_assert(sizeof(BvhLib::Node) == 32u, "trace.slang declares Node as 32 bytes.");
    static_assert(sizeof(BvhLib::Triangle) == 48u, "trace.slang declares Triangle as 48 bytes.");

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
        return words;
    }

} // namespace

Tracer::Tracer(const Device& device, const BvhLib::Bvh& bvh, const std::vector<Material>& materials, uint32_t frames_in_flight, const std::string& shader_path,
    LoggingLib::Logger& logger) :
    m_device(&device),
    m_logger(&logger),
    m_frames_in_flight(frames_in_flight),
    m_node_count(static_cast<uint32_t>(bvh.nodes.size())),
    m_triangle_count(static_cast<uint32_t>(bvh.triangles.size()))
{
    if (materials.empty()) {
        throw std::runtime_error{"The tracer needs at least one material."};
    }

    // A storage buffer of size zero is not legal, so an empty world still gets one element of each.
    // The shader checks node_count and renders black rather than reading them.
    const BvhLib::Node placeholder_node{};
    const BvhLib::Triangle placeholder_triangle{};

    m_nodes = uploadStorageBuffer(bvh.nodes.empty() ? static_cast<const void*>(&placeholder_node) : static_cast<const void*>(bvh.nodes.data()),
        bvh.nodes.empty() ? sizeof(BvhLib::Node) : (bvh.nodes.size() * sizeof(BvhLib::Node)));

    m_triangles = uploadStorageBuffer(bvh.triangles.empty() ? static_cast<const void*>(&placeholder_triangle) : static_cast<const void*>(bvh.triangles.data()),
        bvh.triangles.empty() ? sizeof(BvhLib::Triangle) : (bvh.triangles.size() * sizeof(BvhLib::Triangle)));

    m_materials = uploadStorageBuffer(materials.data(), materials.size() * sizeof(Material));

    const vk::DeviceSize total_bytes{(static_cast<vk::DeviceSize>(m_node_count) * sizeof(BvhLib::Node))
        + (static_cast<vk::DeviceSize>(m_triangle_count) * sizeof(BvhLib::Triangle)) + (materials.size() * sizeof(Material))};
    m_logger->logInfo("World uploaded: " + std::to_string(m_triangle_count) + " triangles, " + std::to_string(m_node_count) + " hierarchy nodes, "
        + std::to_string(materials.size()) + " materials, " + std::to_string(total_bytes / 1024u) + " KiB of device-local storage.");

    const std::array<vk::DescriptorSetLayoutBinding, 4> bindings{
        vk::DescriptorSetLayoutBinding{
            .binding = 0u, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{
            .binding = 1u, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{
            .binding = 2u, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{
            .binding = 3u, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute}};

    m_set_layout = vk::raii::DescriptorSetLayout{
        m_device->get(), vk::DescriptorSetLayoutCreateInfo{.bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data()}};

    const std::array<vk::DescriptorPoolSize, 2> pool_sizes{vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageImage, .descriptorCount = m_frames_in_flight},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = 3u * m_frames_in_flight}};

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

    const std::vector<uint32_t> code{readSpirv(shader_path)};
    const vk::raii::ShaderModule module{m_device->get(), vk::ShaderModuleCreateInfo{.codeSize = code.size() * sizeof(uint32_t), .pCode = code.data()}};

    m_pipeline = vk::raii::Pipeline{m_device->get(), nullptr,
        vk::ComputePipelineCreateInfo{.stage = vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eCompute, .module = *module, .pName = "traceMain"},
            .layout = *m_pipeline_layout}};
}

Tracer::DeviceBuffer Tracer::uploadStorageBuffer(const void* data, vk::DeviceSize bytes) const
{
    // Staging into device-local memory rather than leaving the data host-visible. Traversal reads
    // these buffers many times per pixel, and host-visible memory is the wrong side of the bus.
    const vk::raii::Buffer staging_buffer{
        m_device->get(), vk::BufferCreateInfo{.size = bytes, .usage = vk::BufferUsageFlagBits::eTransferSrc, .sharingMode = vk::SharingMode::eExclusive}};

    const vk::MemoryRequirements staging_requirements{staging_buffer.getMemoryRequirements()};
    const vk::raii::DeviceMemory staging_memory{m_device->get(),
        vk::MemoryAllocateInfo{.allocationSize = staging_requirements.size,
            .memoryTypeIndex = findMemoryType(m_device->physicalDevice(), staging_requirements.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)}};
    staging_buffer.bindMemory(*staging_memory, 0u);

    void* mapped{staging_memory.mapMemory(0u, bytes)};
    std::memcpy(mapped, data, static_cast<size_t>(bytes));
    staging_memory.unmapMemory();

    DeviceBuffer result{};
    result.buffer = vk::raii::Buffer{m_device->get(),
        vk::BufferCreateInfo{
            .size = bytes, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .sharingMode = vk::SharingMode::eExclusive}};

    const vk::MemoryRequirements requirements{result.buffer.getMemoryRequirements()};
    result.memory = vk::raii::DeviceMemory{m_device->get(),
        vk::MemoryAllocateInfo{.allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(m_device->physicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)}};
    result.buffer.bindMemory(*result.memory, 0u);

    const vk::raii::CommandPool pool{
        m_device->get(), vk::CommandPoolCreateInfo{.flags = vk::CommandPoolCreateFlagBits::eTransient, .queueFamilyIndex = m_device->graphicsFamilyIndex()}};

    vk::raii::CommandBuffers command_buffers{
        m_device->get(), vk::CommandBufferAllocateInfo{.commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1u}};
    const vk::raii::CommandBuffer& command_buffer{command_buffers.front()};

    command_buffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    command_buffer.copyBuffer(*staging_buffer, *result.buffer, {vk::BufferCopy{.srcOffset = 0u, .dstOffset = 0u, .size = bytes}});
    command_buffer.end();

    const vk::raii::Fence fence{m_device->get(), vk::FenceCreateInfo{}};
    m_device->graphicsQueue().submit({vk::SubmitInfo{.commandBufferCount = 1u, .pCommandBuffers = &*command_buffer}}, *fence);

    while (m_device->get().waitForFences({*fence}, vk::True, UINT64_MAX) == vk::Result::eTimeout) {
        // Retry — a timeout here is not an error.
    }

    return result;
}

void Tracer::createOutputImages()
{
    m_output_views.clear();
    m_output_images.clear();
    m_output_memory.clear();

    for (uint32_t frame{0u}; frame < m_frames_in_flight; ++frame) {
        vk::raii::Image image{m_device->get(),
            vk::ImageCreateInfo{.imageType = vk::ImageType::e2D,
                .format = OUTPUT_FORMAT,
                .extent = vk::Extent3D{m_extent.width, m_extent.height, 1u},
                .mipLevels = 1u,
                .arrayLayers = 1u,
                .samples = vk::SampleCountFlagBits::e1,
                .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
                .sharingMode = vk::SharingMode::eExclusive,
                .initialLayout = vk::ImageLayout::eUndefined}};

        const vk::MemoryRequirements requirements{image.getMemoryRequirements()};
        vk::raii::DeviceMemory memory{m_device->get(),
            vk::MemoryAllocateInfo{.allocationSize = requirements.size,
                .memoryTypeIndex = findMemoryType(m_device->physicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)}};
        image.bindMemory(*memory, 0u);

        vk::raii::ImageView view{m_device->get(),
            vk::ImageViewCreateInfo{.image = *image,
                .viewType = vk::ImageViewType::e2D,
                .format = OUTPUT_FORMAT,
                .subresourceRange = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}}};

        m_output_images.emplace_back(std::move(image));
        m_output_memory.emplace_back(std::move(memory));
        m_output_views.emplace_back(std::move(view));
    }
}

void Tracer::writeDescriptorSets()
{
    for (uint32_t frame{0u}; frame < m_frames_in_flight; ++frame) {
        const vk::DescriptorImageInfo image_info{.imageView = *m_output_views[frame], .imageLayout = vk::ImageLayout::eGeneral};

        const vk::DescriptorBufferInfo nodes_info{.buffer = *m_nodes.buffer, .offset = 0u, .range = vk::WholeSize};
        const vk::DescriptorBufferInfo triangles_info{.buffer = *m_triangles.buffer, .offset = 0u, .range = vk::WholeSize};
        const vk::DescriptorBufferInfo materials_info{.buffer = *m_materials.buffer, .offset = 0u, .range = vk::WholeSize};

        const std::array<vk::WriteDescriptorSet, 4> writes{vk::WriteDescriptorSet{.dstSet = *m_descriptor_sets[frame],
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
                .pBufferInfo = &materials_info}};

        m_device->get().updateDescriptorSets(writes, {});
    }
}

void Tracer::resize(vk::Extent2D extent)
{
    m_extent = extent;
    createOutputImages();
    writeDescriptorSets();
}

void Tracer::record(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot, const Camera& camera, uint32_t max_bounces, float exposure) const
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
        .node_count = m_node_count,
        .exposure = exposure};

    command_buffer.pushConstants<TracePushConstants>(*m_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0u, {push});

    const uint32_t groups_x{(m_extent.width + WORKGROUP_SIZE - 1u) / WORKGROUP_SIZE};
    const uint32_t groups_y{(m_extent.height + WORKGROUP_SIZE - 1u) / WORKGROUP_SIZE};
    command_buffer.dispatch(groups_x, groups_y, 1u);

    const vk::ImageMemoryBarrier2 to_transfer_src{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eBlit,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *m_output_images[frame_slot],
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}};
    command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &to_transfer_src});
}
