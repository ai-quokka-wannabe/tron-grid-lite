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
#include <algorithm>
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
        uint32_t instance_count;
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

    /*
        The rest of the shared constants, asserted for the same reason and against the same literals
        acoustics.slang carries. None of these would fail loudly if they drifted: a different bin
        width or speed of sound simply puts every arrival in the wrong place, and a different surface
        epsilon changes which surfaces a reflected ray re-hits. The host and the device would each
        be self-consistent and disagree with each other, which is exactly the shape of the direction
        set bug --verify-acoustics caught.
    */
    static_assert(Acoustics::BIN_SECONDS == 0.001f, "acoustics.slang declares BIN_SECONDS as 0.001.");
    static_assert(Acoustics::SPEED_OF_SOUND == 343.0f, "acoustics.slang declares SPEED_OF_SOUND as 343.0.");
    static_assert(Acoustics::SURFACE_EPSILON == 1e-3f, "acoustics.slang declares SURFACE_EPSILON as 1e-3.");

    /*
        The one that matters most.

        Every acoustic ray's heading comes from this number, so of all the constants duplicated across
        this boundary it is the one whose drift the verification is most sensitive to. The two sides
        agreeing to the last bit is worth a factor of three hundred in the host-device disagreement,
        which is what lets the acceptance threshold sit at a tenth of a per cent.
    */
    static_assert(Acoustics::GOLDEN_TURN_FRACTION == 0.38196601125010515f, "acoustics.slang declares GOLDEN_TURN_FRACTION as 0.38196601125010515.");

    //! Creates a storage buffer and binds it into an arena, returning its mapped address if any.
    void* createArenaBuffer(const Device& device, MemoryArena& arena, vk::DeviceSize bytes, vk::raii::Buffer& buffer)
    {
        buffer = vk::raii::Buffer{
            device.get(), vk::BufferCreateInfo{.size = bytes, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .sharingMode = vk::SharingMode::eExclusive}};
        return arena.bind(buffer);
    }

    //! Block size for the two host-visible buffers. Both are tiny; one block holds any sane ear count.
    constexpr vk::DeviceSize HOST_BLOCK_BYTES{1u * 1024u * 1024u};

    //! Block size for the device-local source-strength table, which is a handful of floats.
    constexpr vk::DeviceSize DEVICE_BLOCK_BYTES{1u * 1024u * 1024u};

} // namespace

AcousticTracer::AcousticTracer(const Device& device, const World& world, const std::vector<float>& source_strengths, uint32_t max_ears, const std::string& shader_path,
    LoggingLib::Logger& logger) :
    m_device(&device),
    m_world(&world),
    m_logger(&logger),
    m_max_ears(max_ears),
    m_material_count(static_cast<uint32_t>(source_strengths.size())),
    m_loudest_source(source_strengths.empty() ? 0.0f : *std::max_element(source_strengths.begin(), source_strengths.end())),
    m_device_arena(device, vk::MemoryPropertyFlagBits::eDeviceLocal, DEVICE_BLOCK_BYTES),
    m_host_arena(device, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, HOST_BLOCK_BYTES)
{
    if (source_strengths.empty()) {
        throw std::runtime_error{"The acoustic pass needs at least one source strength."};
    }

    if (max_ears == 0u) {
        throw std::runtime_error{"The acoustic pass needs at least one ear."};
    }

    m_source_strengths = VulkanHelpers::uploadStorageBuffer(device, m_device_arena, source_strengths.data(), source_strengths.size() * sizeof(float));

    const vk::DeviceSize ear_bytes{static_cast<vk::DeviceSize>(max_ears) * sizeof(MathLib::Vec4)};
    const vk::DeviceSize histogram_bytes{static_cast<vk::DeviceSize>(max_ears) * HISTOGRAM_ENTRIES * sizeof(uint32_t)};

    // Both land in the same host-visible block, which the arena maps once: one allocation and one
    // mapping for the pair, and each buffer's address is an interior pointer into it.
    m_ears_mapped = createArenaBuffer(device, m_host_arena, ear_bytes, m_ears);
    m_histogram_mapped = createArenaBuffer(device, m_host_arena, histogram_bytes, m_histogram);

    // Ears default to the origin rather than to whatever the allocator left behind, so a dispatch
    // recorded before setEars gathers from a defined place instead of from uninitialised floats.
    std::memset(m_ears_mapped, 0, static_cast<size_t>(ear_bytes));
    std::memset(m_histogram_mapped, 0, static_cast<size_t>(histogram_bytes));

    m_logger->logInfo("Acoustic pass allocated: " + std::to_string(max_ears) + " ears, " + std::to_string(Acoustics::BAND_COUNT) + " bands by "
        + std::to_string(Acoustics::BIN_COUNT) + " bins, " + std::to_string(histogram_bytes) + " bytes of histogram.");

    std::array<vk::DescriptorSetLayoutBinding, 6> bindings{};
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
    const std::array<vk::DescriptorBufferInfo, 6> buffer_infos{vk::DescriptorBufferInfo{.buffer = m_world->nodes(), .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = m_world->triangles(), .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = *m_source_strengths.buffer, .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = *m_ears, .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = *m_histogram, .offset = 0u, .range = vk::WholeSize},
        vk::DescriptorBufferInfo{.buffer = m_world->instances(), .offset = 0u, .range = vk::WholeSize}};

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
        Check the fixed-point product before dispatching, rather than discovering the wrap in a
        histogram where it would read as an arrival quieter than the one before it.

        The worst case is every ray depositing into the same bin at the loudest amplitude the inputs
        allow: one deposit per ray per segment, and a ray makes `max_order + 1` segments — the direct
        one plus one per reflection. Both the source table and the band spectrum are authored by the
        caller and neither is bounded by anything, so both belong in the product; spreading and air
        can only ever attenuate, so they are safely ignored.
    */
    const float loudest_band{std::max({config.hum_spectrum[0], config.hum_spectrum[1], config.hum_spectrum[2], config.hum_spectrum[3]})};
    const double loudest_deposit{static_cast<double>(m_loudest_source) * static_cast<double>(loudest_band) * static_cast<double>(FIXED_POINT_SCALE)};

    const uint64_t segments{static_cast<uint64_t>(config.direction_count) * (static_cast<uint64_t>(config.max_order) + 1u)};
    const double worst_case_total{static_cast<double>(segments) * loudest_deposit};

    if (worst_case_total > static_cast<double>(UINT32_MAX)) {
        throw std::runtime_error{"Acoustic ray budget can overflow the fixed-point histogram: " + std::to_string(segments) + " segments at up to "
            + std::to_string(static_cast<double>(m_loudest_source * loudest_band)) + " each."};
    }

    const AcousticPushConstants push{.hum_spectrum = MathLib::Vec4{config.hum_spectrum[0], config.hum_spectrum[1], config.hum_spectrum[2], config.hum_spectrum[3]},
        .air_absorption_db_per_km = MathLib::Vec4{config.air_absorption_db_per_km[0], config.air_absorption_db_per_km[1], config.air_absorption_db_per_km[2],
            config.air_absorption_db_per_km[3]},
        .direction_count = config.direction_count,
        .max_order = config.max_order,
        .instance_count = m_world->instanceCount(),
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

    /*
        Make the histogram visible to the host.

        **A fence is not enough, and host-coherent memory is not enough either.** Waiting on a fence
        establishes that the work finished; it does not make the shader's writes *available* to the
        host memory domain, and coherence only removes the need to invalidate a mapped range, not the
        need for the dependency itself. Without this the host may read a stale histogram, and the
        specification permits it to — the fact that it happens to work on the driver in front of me
        is the least reliable evidence available.

        `recordCinematic` in `main.cpp` does exactly this for its image readback: the same dependency
        for the same reason, and the twin to check this one against.

        There is deliberately no matching barrier for the ear positions going the other way, and the
        asymmetry is real rather than an oversight: submitting to a queue defines a memory dependency
        with prior host writes to mapped memory, so the device sees them without being told. Nothing
        in the specification does the same favour in reverse.
    */
    const vk::MemoryBarrier2 histogram_visible{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eHost,
        .dstAccessMask = vk::AccessFlagBits2::eHostRead};
    command_buffer.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1u, .pMemoryBarriers = &histogram_visible});
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
