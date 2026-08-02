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

#include "acoustic_tracer.hpp"
#include "device.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace
{

    using VulkanHelpers::findMemoryType;
    using VulkanHelpers::readSpirv;

    //! Fixed-point scale. Must equal FIXED_POINT_SCALE in acoustics.slang.
    constexpr float FIXED_POINT_SCALE{262144.0f};

    /*!
        Mirrors AcousticPushConstants in acoustics.slang exactly.

        The two `Vec4`s come first so that both sit at a 16-byte offset, which is what the std430
        rules a push constant block follows require of a `float4`.
    */
    struct AcousticPushConstants {
        MathLib::Vec4 hum_spectrum;
        MathLib::Vec4 air_absorption_db_per_km;
        uint32_t direction_count;
        uint32_t max_order;
        uint32_t node_count;
        uint32_t material_count;
        float range_metres;
    };

    static_assert(sizeof(AcousticPushConstants) == 52u, "Push constants must match the Slang struct layout exactly.");
    static_assert(offsetof(AcousticPushConstants, air_absorption_db_per_km) == 16u, "air_absorption must sit at offset 16.");
    static_assert(offsetof(AcousticPushConstants, direction_count) == 32u, "direction_count must sit at offset 32.");
    static_assert(offsetof(AcousticPushConstants, range_metres) == 48u, "range_metres must sit at offset 48.");

    // The shader hard-codes these because a groupshared array needs a compile-time size. If the host
    // ever disagrees, the histogram is read at the wrong stride and every band but the first is
    // nonsense — which is exactly the kind of failure that looks like an acoustics problem.
    static_assert(Acoustics::BAND_COUNT == 4u, "acoustics.slang declares BAND_COUNT as 4.");
    static_assert(Acoustics::BIN_COUNT == 64u, "acoustics.slang declares BIN_COUNT as 64.");

    //! Creates a host-visible, host-coherent buffer and leaves it permanently mapped.
    void createMappedBuffer(const Device& device, vk::DeviceSize bytes, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& memory, void*& mapped)
    {
        buffer = vk::raii::Buffer{
            device.get(), vk::BufferCreateInfo{.size = bytes, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .sharingMode = vk::SharingMode::eExclusive}};

        const vk::MemoryRequirements requirements{buffer.getMemoryRequirements()};
        memory = vk::raii::DeviceMemory{device.get(),
            vk::MemoryAllocateInfo{.allocationSize = requirements.size,
                .memoryTypeIndex = findMemoryType(device.physicalDevice(), requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)}};
        buffer.bindMemory(*memory, 0u);

        /*
            Mapped once and never unmapped. Vulkan explicitly permits a persistent mapping, and the
            alternative — mapping and unmapping around every solve — costs a driver round trip per
            access to buy nothing, since nothing else may touch the memory while a dispatch is in
            flight either way.
        */
        mapped = memory.mapMemory(0u, bytes);
    }

} // namespace

AcousticTracer::AcousticTracer(const Device& device, const World& world, const std::vector<float>& source_strengths, uint32_t max_ears, const std::string& shader_path,
    LoggingLib::Logger& logger) :
    m_device(&device),
    m_world(&world),
    m_logger(&logger),
    m_max_ears(max_ears),
    m_material_count(static_cast<uint32_t>(source_strengths.size()))
{
    if (source_strengths.empty()) {
        throw std::runtime_error{"The acoustic pass needs at least one source strength."};
    }

    if (max_ears == 0u) {
        throw std::runtime_error{"The acoustic pass needs at least one ear."};
    }

    m_source_strengths = VulkanHelpers::uploadStorageBuffer(device, source_strengths.data(), source_strengths.size() * sizeof(float));

    const vk::DeviceSize ear_bytes{static_cast<vk::DeviceSize>(max_ears) * sizeof(MathLib::Vec4)};
    const vk::DeviceSize histogram_bytes{static_cast<vk::DeviceSize>(max_ears) * HISTOGRAM_ENTRIES * sizeof(uint32_t)};

    createMappedBuffer(device, ear_bytes, m_ears, m_ears_memory, m_ears_mapped);
    createMappedBuffer(device, histogram_bytes, m_histogram, m_histogram_memory, m_histogram_mapped);

    // Ears default to the origin rather than to whatever the allocator left behind, so a dispatch
    // recorded before setEars gathers from a defined place instead of from uninitialised floats.
    std::memset(m_ears_mapped, 0, static_cast<size_t>(ear_bytes));
    std::memset(m_histogram_mapped, 0, static_cast<size_t>(histogram_bytes));

    m_logger->logInfo("Acoustic pass allocated: " + std::to_string(max_ears) + " ears, " + std::to_string(Acoustics::BAND_COUNT) + " bands by "
        + std::to_string(Acoustics::BIN_COUNT) + " bins, " + std::to_string(histogram_bytes) + " bytes of histogram.");

    std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t index{0u}; index < bindings.size(); ++index) {
        bindings[index] = vk::DescriptorSetLayoutBinding{
            .binding = index, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1u, .stageFlags = vk::ShaderStageFlagBits::eCompute};
    }

    m_set_layout = vk::raii::DescriptorSetLayout{
        m_device->get(), vk::DescriptorSetLayoutCreateInfo{.bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data()}};

    const vk::DescriptorPoolSize pool_size{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = static_cast<uint32_t>(bindings.size())};

    // eFreeDescriptorSet is required rather than optional: vk::raii::DescriptorSets frees its sets
    // when destroyed, and vkFreeDescriptorSets against a pool created without this flag is a
    // validation error at shutdown.
    m_descriptor_pool = vk::raii::DescriptorPool{m_device->get(),
        vk::DescriptorPoolCreateInfo{.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, .maxSets = 1u, .poolSizeCount = 1u, .pPoolSizes = &pool_size}};

    m_descriptor_sets = vk::raii::DescriptorSets{
        m_device->get(), vk::DescriptorSetAllocateInfo{.descriptorPool = *m_descriptor_pool, .descriptorSetCount = 1u, .pSetLayouts = &*m_set_layout}};

    // One set, written once: unlike the renderer's output images, none of these buffers is
    // per-frame, so there is nothing here that a resize or a frame index could invalidate.
    const std::array<vk::DescriptorBufferInfo, 5> buffer_infos{vk::DescriptorBufferInfo{.buffer = m_world->nodes(), .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = m_world->triangles(), .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = *m_source_strengths.buffer, .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = *m_ears, .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = *m_histogram, .offset = 0u, .range = vk::WholeSize}};

    std::array<vk::WriteDescriptorSet, 5> writes{};
    for (uint32_t index{0u}; index < writes.size(); ++index) {
        writes[index] = vk::WriteDescriptorSet{.dstSet = *m_descriptor_sets[0],
            .dstBinding = index,
            .dstArrayElement = 0u,
            .descriptorCount = 1u,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo = &buffer_infos[index]};
    }
    m_device->get().updateDescriptorSets(writes, {});

    const vk::PushConstantRange push_range{.stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0u, .size = sizeof(AcousticPushConstants)};
    m_pipeline_layout = vk::raii::PipelineLayout{m_device->get(),
        vk::PipelineLayoutCreateInfo{.setLayoutCount = 1u, .pSetLayouts = &*m_set_layout, .pushConstantRangeCount = 1u, .pPushConstantRanges = &push_range}};

    const std::vector<uint32_t> code{readSpirv(shader_path)};
    const vk::raii::ShaderModule module{m_device->get(), vk::ShaderModuleCreateInfo{.codeSize = code.size() * sizeof(uint32_t), .pCode = code.data()}};

    m_pipeline = vk::raii::Pipeline{m_device->get(), nullptr,
        vk::ComputePipelineCreateInfo{.stage = vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eCompute, .module = *module, .pName = "acousticMain"},
            .layout = *m_pipeline_layout}};
}

void AcousticTracer::setEars(const std::vector<MathLib::Vec3>& ears)
{
    if (ears.size() > m_max_ears) {
        throw std::runtime_error{"More ears than this acoustic pass was built for."};
    }

    // The shader reads float4 because a StructuredBuffer of float3 would be padded to 16 bytes
    // anyway; writing the padding explicitly keeps the host and the device agreeing about stride.
    std::vector<MathLib::Vec4> padded;
    padded.reserve(ears.size());
    for (const MathLib::Vec3& ear : ears) {
        padded.push_back(MathLib::Vec4::fromVec3(ear, 0.0f));
    }

    std::memcpy(m_ears_mapped, padded.data(), padded.size() * sizeof(MathLib::Vec4));
}

void AcousticTracer::record(const vk::raii::CommandBuffer& command_buffer, uint32_t ear_count, const Acoustics::GatherConfig& config) const
{
    if ((ear_count == 0u) || (ear_count > m_max_ears)) {
        throw std::runtime_error{"Acoustic dispatch asked for an ear count this pass was not built for."};
    }

    /*
        Assert the fixed-point product before dispatching rather than discovering the wrap in a
        histogram, where it would read as an arrival that is quieter than the one before it.

        The worst case is every ray depositing at full strength into the same bin: one deposit per
        ray per reflection order, all landing together. `2^18` per unit arrival leaves room for
        `2^31 / 2^18 = 8,192` of them, which the default budget of 2,048 directions by four orders
        reaches exactly.
    */
    const uint64_t worst_case_deposits{static_cast<uint64_t>(config.direction_count) * (static_cast<uint64_t>(config.max_order) + 1u)};
    const uint64_t worst_case_total{worst_case_deposits * static_cast<uint64_t>(FIXED_POINT_SCALE)};
    if (worst_case_total > UINT32_MAX) {
        throw std::runtime_error{"Acoustic ray budget can overflow the fixed-point histogram: " + std::to_string(worst_case_deposits)
            + " full-strength deposits against a ceiling of " + std::to_string(UINT32_MAX / static_cast<uint32_t>(FIXED_POINT_SCALE)) + "."};
    }

    const AcousticPushConstants push{.hum_spectrum = MathLib::Vec4{config.hum_spectrum[0], config.hum_spectrum[1], config.hum_spectrum[2], config.hum_spectrum[3]},
        .air_absorption_db_per_km = MathLib::Vec4{config.air_absorption_db_per_km[0], config.air_absorption_db_per_km[1], config.air_absorption_db_per_km[2],
            config.air_absorption_db_per_km[3]},
        .direction_count = config.direction_count,
        .max_order = config.max_order,
        .node_count = m_world->nodeCount(),
        .material_count = m_material_count,
        .range_metres = config.range_metres};

    command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *m_pipeline);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_pipeline_layout, 0u, {*m_descriptor_sets[0]}, {});
    command_buffer.pushConstants<AcousticPushConstants>(*m_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0u, {push});

    /*
        One workgroup per ear, which is what makes the histogram a shared-memory reduction with no
        cross-workgroup contention at all.

        The workgroup's own size is deliberately not named here. It is 64 threads, declared once in
        `acoustics.slang`'s `numthreads`, and the host has no business knowing it: the dispatch is
        counted in ears, and each thread strides over the direction set by itself. A copy of the
        number on this side would be a second place for it to be wrong.
    */
    command_buffer.dispatch(ear_count, 1u, 1u);
}

Acoustics::ImpulseResponse AcousticTracer::read(uint32_t ear_index) const
{
    if (ear_index >= m_max_ears) {
        throw std::runtime_error{"Acoustic readback asked for an ear this pass was not built for."};
    }

    Acoustics::ImpulseResponse response{};

    const uint32_t* const fixed{static_cast<const uint32_t*>(m_histogram_mapped) + (static_cast<size_t>(ear_index) * HISTOGRAM_ENTRIES)};
    for (uint32_t entry{0u}; entry < HISTOGRAM_ENTRIES; ++entry) {
        response.bins[entry] = static_cast<float>(fixed[entry]) / FIXED_POINT_SCALE;
    }

    return response;
}
