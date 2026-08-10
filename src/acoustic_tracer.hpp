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

#pragma once

#ifdef _WIN32
#include <Volk/volk.h>
#else
#include <volk/volk.h>
#endif
#include <vulkan/vulkan_raii.hpp>
#include "acoustics.hpp"
#include "memory_arena.hpp"
#include "vulkan_helpers.hpp"
#include "world.hpp"
#include <logging/logger.hpp>
#include <math/vector.hpp>
#include <cstdint>
#include <string>
#include <vector>

class Device; // forward declaration

/*!
    The acoustic pass: a compute shader that gathers sound at a set of ears.

    The acoustic twin of `Tracer`, and deliberately shaped like it — but the two share far less than
    they appear to, and the part they do share is the expensive part. Both bind the same `World`, and
    both traverse it through the same `grid_bvh` Slang module with the same slab test and the same
    Möller-Trumbore intersection. Everything downstream of the intersection differs: this pass has no
    image, no materials in the optical sense, and no notion of colour. What it produces is an impulse
    response per ear — energy against delay — accumulated into a fixed-point histogram.

    **One workgroup owns one ear.** Its histogram lives in shared memory as `uint`s, 4 bands by
    64 bins, which is 1 KiB against the 16 KiB Vulkan guarantees a workgroup. That removes
    cross-workgroup atomic contention entirely and, because integer addition is associative, makes
    the result bit-identical however the threads are scheduled. A float atomic would have made the
    answer depend on the order the hardware happened to finish in, which the replay guarantee in
    `docs/PROGRAM_INTERFACE.md` could not survive — so the absence of `VK_EXT_shader_atomic_float`
    turns out to be a feature rather than a constraint worked around.

    `src/acoustics.hpp` is the specification this mirrors, and `--verify-acoustics` compares the two
    on the real Grid. They are not expected to agree bit for bit — no two implementations of `cos`
    agree bit for bit — but they are expected to agree to well within a bin, and a disagreement
    larger than that is a bug in one of them.

    Both the ear positions and the histogram live in host-visible memory. That is the right choice
    rather than a shortcut: they are a few kilobytes, they are written or read by the host on every
    solve, and staging them would cost two copies and a fence to avoid a transfer that is smaller
    than the command buffer describing it.
*/
class AcousticTracer {
public:
    /*!
        Builds the acoustic pipeline and its buffers.

        \param device Logical device.
        \param world The Grid's geometry. Must outlive this object; an empty one gathers silence.
        \param source_strengths How loudly each material sings, indexed by a triangle's material.
        \param max_ears Ears this may ever be dispatched for. Sizes the ear and histogram buffers.
        \param shader_path Path to the compiled `acoustics.spv`.
        \param logger Logger for the allocation summary.
    */
    AcousticTracer(const Device& device, const World& world, const std::vector<float>& source_strengths, uint32_t max_ears, const std::string& shader_path,
        LoggingLib::Logger& logger);

    // Non-copyable, non-movable: it owns Vulkan objects that recorded command buffers refer to.
    AcousticTracer(const AcousticTracer&) = delete;
    AcousticTracer& operator=(const AcousticTracer&) = delete;
    AcousticTracer(AcousticTracer&&) = delete;
    AcousticTracer& operator=(AcousticTracer&&) = delete;

    /*!
        Writes the ear positions the next dispatch will gather at.

        Host-visible and coherent, so this is a memcpy with no flush and no barrier. It must not be
        called while a dispatch reading the same buffer is still in flight — the caller owns that
        ordering, as it owns the rest of the frame's timeline.

        \param ears World-space ear positions, in metres. At most `maxEars()`.
    */
    void setEars(const std::vector<MathLib::Vec3>& ears);

    /*!
        Records one dispatch: one workgroup per ear placed by the last `setEars`.

        **The ear count is not a parameter, and that is the point.** It was one, and every caller
        passed the same number it had already given the constructor and then given `setEars` — the
        same fact in three places, held together by two runtime checks that existed only because it
        could disagree with itself. A dispatch gathers for the ears that are actually there.

        \param command_buffer Command buffer to record into.
        \param config Spectrum, air absorption, ray budget and caps.
        \throws std::runtime_error if the fixed-point scale could overflow at this ray budget.
    */
    void record(const vk::raii::CommandBuffer& command_buffer, const Acoustics::GatherConfig& config) const;

    /*!
        Reads back one ear's impulse response, converting out of fixed point.

        The caller must have waited for the dispatch to complete. Nothing here synchronises: the
        memory is coherent, so what is missing is the GPU having finished writing it, and only the
        caller knows that.

        \param ear_index Which ear, below `earCount()`.
        \return Energy per band per time bin.
    */
    [[nodiscard]] Acoustics::ImpulseResponse read(uint32_t ear_index) const;

    //! Returns the number of ears this was built for. The most `setEars` will accept.
    [[nodiscard]] uint32_t maxEars() const
    {
        return m_max_ears;
    }

    //! Returns the number of ears currently placed. Zero until `setEars`.
    [[nodiscard]] uint32_t earCount() const
    {
        return m_ear_count;
    }

private:
    //! Entries in one ear's histogram.
    static constexpr uint32_t HISTOGRAM_ENTRIES{Acoustics::BAND_COUNT * Acoustics::BIN_COUNT};

    const Device* m_device{nullptr}; //!< Logical device (non-owning).
    const World* m_world{nullptr}; //!< The Grid's geometry (non-owning). Shared with the renderer.
    LoggingLib::Logger* m_logger{nullptr}; //!< Logger (non-owning).

    uint32_t m_max_ears{0u}; //!< Ears the buffers were sized for.
    uint32_t m_ear_count{0u}; //!< Ears actually placed by setEars. Never above m_max_ears.
    uint32_t m_material_count{0u}; //!< Length of the source-strength table, for the shader's bounds guard.

    /*!
        The loudest entry in the source table, for the fixed-point overflow check.

        Kept because `record` cannot see the table — it lives on the device by then — and the check
        is meaningless without it. A caller is free to author a strength above one, and the histogram
        would wrap silently rather than complain.
    */
    float m_loudest_source{0.0f};

    //! Device-local block behind the source-strength table. Declared before it so it outlives it.
    MemoryArena m_device_arena;

    /*!
        Host-visible block behind the ear and histogram buffers.

        Both are tiny — thirty-two bytes and two kilobytes at two ears — and both are written or read
        by the host on every solve, so one shared mapping serves them better than two allocations.
    */
    MemoryArena m_host_arena;

    VulkanHelpers::DeviceBuffer m_source_strengths; //!< One float per material slot. Device-local; never changes.

    vk::raii::Buffer m_ears{nullptr}; //!< Ear positions, host-visible.
    void* m_ears_mapped{nullptr}; //!< Mapped ear buffer.

    vk::raii::Buffer m_histogram{nullptr}; //!< Fixed-point histograms, host-visible.
    void* m_histogram_mapped{nullptr}; //!< Mapped histogram buffer.

    vk::raii::DescriptorSetLayout m_set_layout{nullptr}; //!< Layout of the single descriptor set.
    vk::raii::DescriptorPool m_descriptor_pool{nullptr}; //!< Pool the set comes from.
    vk::raii::DescriptorSets m_descriptor_sets{nullptr}; //!< One set; the buffers never change.

    vk::raii::PipelineLayout m_pipeline_layout{nullptr}; //!< Layout carrying the push constants.
    vk::raii::Pipeline m_pipeline{nullptr}; //!< The compute pipeline itself.
};
