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

#include "senses_tracer.hpp"
#include "device.hpp"
#include "spirv.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace
{

    //! Mirrors SensesPushConstants in senses.slang exactly.
    struct SensesPushConstants {
        uint32_t ray_count;
        uint32_t max_bounces;
        uint32_t instance_count;
    };

    static_assert(sizeof(SensesPushConstants) == 12u, "Push constants must match the Slang struct layout exactly.");
    static_assert(offsetof(SensesPushConstants, max_bounces) == 4u, "max_bounces must sit at offset 4.");
    static_assert(offsetof(SensesPushConstants, instance_count) == 8u, "instance_count must sit at offset 8.");

    //! Block size for the two host-visible buffers. Generous against any sane per-tick ray count.
    constexpr vk::DeviceSize HOST_BLOCK_BYTES{1u * 1024u * 1024u};

    //! Block size for the device-local material table, which is six 32-byte rows.
    constexpr vk::DeviceSize DEVICE_BLOCK_BYTES{1u * 1024u * 1024u};

} // namespace

SensesTracer::SensesTracer(const Device& device, const World& world, const std::vector<Material>& materials, uint32_t max_rays, const std::string& shader_path,
    LoggingLib::Logger& logger) :
    m_device(&device),
    m_world(&world),
    m_max_rays(max_rays),
    m_device_arena(device, vk::MemoryPropertyFlagBits::eDeviceLocal, DEVICE_BLOCK_BYTES),
    m_host_arena(device, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, HOST_BLOCK_BYTES)
{
    if (materials.empty()) {
        throw std::runtime_error{"The senses pass needs a material table to shade with."};
    }

    if (max_rays == 0u) {
        throw std::runtime_error{"The senses pass needs room for at least one ray."};
    }

    m_materials = VulkanHelpers::uploadStorageBuffer(device, m_device_arena, materials.data(), materials.size() * sizeof(Material));

    const vk::DeviceSize ray_bytes{static_cast<vk::DeviceSize>(max_rays) * 2u * sizeof(MathLib::Vec4)};
    const vk::DeviceSize result_bytes{static_cast<vk::DeviceSize>(max_rays) * sizeof(MathLib::Vec4)};

    m_rays = vk::raii::Buffer{
        device.get(), vk::BufferCreateInfo{.size = ray_bytes, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .sharingMode = vk::SharingMode::eExclusive}};
    m_rays_mapped = m_host_arena.bind(m_rays);

    m_results = vk::raii::Buffer{
        device.get(), vk::BufferCreateInfo{.size = result_bytes, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .sharingMode = vk::SharingMode::eExclusive}};
    m_results_mapped = m_host_arena.bind(m_results);

    // Defined contents before the first solve, for the same reason the acoustic pass zeroes its
    // buffers: a dispatch that somehow precedes the first write reads zeros rather than garbage.
    std::memset(m_rays_mapped, 0, static_cast<size_t>(ray_bytes));
    std::memset(m_results_mapped, 0, static_cast<size_t>(result_bytes));

    logger.logInfo("Senses pass allocated: room for " + std::to_string(max_rays) + " sample rays, " + std::to_string(materials.size()) + " materials.");

    std::array<vk::DescriptorSetLayoutBinding, 6> bindings{};
    for (uint32_t index{0u}; index < bindings.size(); ++index) {
        bindings[index] = vk::DescriptorSetLayoutBinding{
            .binding = index, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute};
    }

    m_set_layout = vk::raii::DescriptorSetLayout{
        m_device->get(), vk::DescriptorSetLayoutCreateInfo{.bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data()}};

    const vk::DescriptorPoolSize pool_size{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = static_cast<uint32_t>(bindings.size())};

    // eFreeDescriptorSet for the same reason the acoustic pass needs it: vk::raii::DescriptorSets
    // frees its sets on destruction, which is only legal against a pool created with the flag.
    m_descriptor_pool = vk::raii::DescriptorPool{m_device->get(),
        vk::DescriptorPoolCreateInfo{.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, .maxSets = 1u, .poolSizeCount = 1u, .pPoolSizes = &pool_size}};

    m_descriptor_sets = vk::raii::DescriptorSets{
        m_device->get(), vk::DescriptorSetAllocateInfo{.descriptorPool = *m_descriptor_pool, .descriptorSetCount = 1u, .pSetLayouts = &*m_set_layout}};

    const std::array<vk::DescriptorBufferInfo, 6> buffer_infos{vk::DescriptorBufferInfo{.buffer = m_world->nodes(), .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = m_world->triangles(), .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = *m_materials.buffer, .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = m_world->instances(), .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = *m_rays, .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = *m_results, .offset = 0u, .range = vk::WholeSize}};

    std::array<vk::WriteDescriptorSet, 6> writes{};
    for (uint32_t index{0u}; index < writes.size(); ++index) {
        writes[index] = vk::WriteDescriptorSet{.dstSet = *m_descriptor_sets[0],
            .dstBinding = index,
            .dstArrayElement = 0u,
            .descriptorCount = 1u,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo = &buffer_infos[index]};
    }
    m_device->get().updateDescriptorSets(writes, {});

    const vk::PushConstantRange push_range{.stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0u, .size = sizeof(SensesPushConstants)};
    m_pipeline_layout = vk::raii::PipelineLayout{m_device->get(),
        vk::PipelineLayoutCreateInfo{.setLayoutCount = 1u, .pSetLayouts = &*m_set_layout, .pushConstantRangeCount = 1u, .pPushConstantRanges = &push_range}};

    const std::vector<uint32_t> code{SpirvLib::read(shader_path)};
    const vk::raii::ShaderModule module{m_device->get(), vk::ShaderModuleCreateInfo{.codeSize = code.size() * sizeof(uint32_t), .pCode = code.data()}};

    m_pipeline = vk::raii::Pipeline{m_device->get(), nullptr,
        vk::ComputePipelineCreateInfo{.stage = vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eCompute, .module = *module, .pName = "senseMain"},
            .layout = *m_pipeline_layout}};

    m_command_pool = vk::raii::CommandPool{
        m_device->get(), vk::CommandPoolCreateInfo{.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = device.graphicsFamilyIndex()}};
    m_command_buffers = vk::raii::CommandBuffers{
        m_device->get(), vk::CommandBufferAllocateInfo{.commandPool = *m_command_pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1u}};
    m_fence = vk::raii::Fence{m_device->get(), vk::FenceCreateInfo{}};
}

std::vector<MathLib::Vec4> SensesTracer::solve(const std::vector<MathLib::Vec4>& rays, uint32_t max_bounces)
{
    if ((rays.size() % 2u) != 0u) {
        throw std::runtime_error{"A senses solve takes origin and direction pairs, so the ray buffer must have even length."};
    }

    const uint32_t ray_count{static_cast<uint32_t>(rays.size() / 2u)};
    if (ray_count == 0u) {
        return {};
    }
    if (ray_count > m_max_rays) {
        throw std::runtime_error{"More sample rays than this senses pass was built for: " + std::to_string(ray_count) + " of " + std::to_string(m_max_rays) + "."};
    }

    std::memcpy(m_rays_mapped, rays.data(), rays.size() * sizeof(MathLib::Vec4));

    const vk::raii::CommandBuffer& command_buffer{m_command_buffers.front()};
    command_buffer.reset();
    command_buffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    const SensesPushConstants push{.ray_count = ray_count, .max_bounces = max_bounces, .instance_count = m_world->instanceCount()};

    command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *m_pipeline);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_pipeline_layout, 0u, {*m_descriptor_sets[0]}, {});
    command_buffer.pushConstants<SensesPushConstants>(*m_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0u, {push});

    // 64 threads per workgroup, declared once in senses.slang's numthreads. The rounding up is why
    // the shader bounds-checks its index.
    command_buffer.dispatch((ray_count + 63u) / 64u, 1u, 1u);

    /*
        Make the results visible to the host. A fence is not enough and host-coherent memory is not
        enough either — the same dependency the acoustic pass records for its histogram, for the
        same reason, and that comment is the full argument.
    */
    const vk::MemoryBarrier2 results_visible{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eHost,
        .dstAccessMask = vk::AccessFlagBits2::eHostRead};
    command_buffer.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1u, .pMemoryBarriers = &results_visible});

    command_buffer.end();

    m_device->get().resetFences({*m_fence});
    m_device->graphicsQueue().submit({vk::SubmitInfo{.commandBufferCount = 1u, .pCommandBuffers = &*command_buffer}}, *m_fence);
    while (m_device->get().waitForFences({*m_fence}, vk::True, UINT64_MAX) == vk::Result::eTimeout) {
        // Retry — a timeout here is not an error.
    }

    std::vector<MathLib::Vec4> results(ray_count);
    std::memcpy(results.data(), m_results_mapped, static_cast<size_t>(ray_count) * sizeof(MathLib::Vec4));
    return results;
}
