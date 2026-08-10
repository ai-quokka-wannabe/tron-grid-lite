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
#include <logging/logger.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

class Device; // forward declaration

/*!
    GPU-profiling pass identifiers. Each pass writes a pair of timestamps (start, end) into
    the shared query pool; the difference is that pass's GPU duration. The order is purely
    cosmetic and chosen so the summary line reads in execution order.

    Passes belonging to phases that are not implemented yet simply record nothing. Their
    queries stay unwritten, the readback reports them as unavailable, and they are shown as
    zero — no special-casing is required at the call site, and enabling a pass later needs
    only a matching begin() / end() pair.
*/
enum class GpuPass : uint32_t {
    Frame = 0, //!< Wraps the whole frame — a sanity check against the sum of the individual passes.
    Trace, //!< Compute ray traversal against the BVH in storage buffers (Phase 2 onwards).
    Sensors, //!< Creature sensor renders — many tiny images per frame (Phase 6).
    Post, //!< Post-processing compute: tonemap and bloom (Phase 4).
    Present, //!< Blit or copy into the acquired swapchain image, plus any layout transitions.
    Count //!< Sentinel — the number of distinct passes profiled per frame. Adding one? `PASS_NAMES` in profiler.cpp will say so.
};

/*!
    Timestamp-based GPU profiler owning a single query pool with one ping-pong slot per frame
    in flight.

    The central claim of this renderer is that it runs comfortably on a modest GPU, so this
    class is how that claim gets checked rather than asserted. It reports a per-pass
    exponential moving average and prints one readable summary line per second.

    Cost is deliberately near zero: the readback happens after the caller has already waited
    on that frame slot's fence, which guarantees the matching submission has completed. No
    separate CPU-GPU synchronisation is introduced and the GPU is never stalled. The results
    are read into a fixed-size member buffer through the user-buffer overload of
    vk::Device::getQueryPoolResults, so no allocation occurs per frame.

    On hardware whose graphics queue family reports zero timestampValidBits the profiler
    disables itself, logs one warning, and every method becomes a no-op.
*/
class GpuProfiler {
public:
    /*!
        Create the query pool for the given device and number of frames in flight.

        The profiler self-disables if the device's graphics queue family does not support
        timestamp queries; in that case no pool is created and all methods do nothing.
    */
    GpuProfiler(const Device& device, uint32_t frames_in_flight, LoggingLib::Logger& logger);

    // Non-copyable, non-movable
    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;
    GpuProfiler(GpuProfiler&&) = delete;
    GpuProfiler& operator=(GpuProfiler&&) = delete;

    /*!
        Reset this frame slot's queries. Must be the first profiler call recorded into the
        command buffer each frame, before any begin(), because a timestamp query must be reset
        before it is written. Also marks the slot as having results worth reading back.
    */
    void resetFrame(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot);

    //! Record the start timestamp of a pass. Safe to call for passes that are never ended; those simply report zero.
    void begin(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot, GpuPass pass);

    //! Record the end timestamp of a pass.
    void end(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot, GpuPass pass);

    /*!
        Read back this frame slot's timestamps and fold them into the moving averages.

        Call this straight after waiting on the slot's in-flight fence and before recording
        the new frame. The fence guarantees that every timestamp actually recorded has
        resolved, so this never blocks. Queries belonging to passes that were not recorded stay
        unavailable, are detected individually, and contribute zero.
    */
    void collect(uint32_t frame_slot);

    //! Smoothed duration of a pass in milliseconds. Returns zero when disabled or not yet sampled.
    [[nodiscard]] float averageMs(GpuPass pass) const;

    /*!
        Emit the summary log line at most once per second. Cheap enough to call every frame —
        it returns immediately until the interval has elapsed.
    */
    void logSummary();

private:
    //! Number of queries per frame slot: a (start, end) pair for every pass.
    static constexpr uint32_t TIMESTAMPS_PER_FRAME{static_cast<uint32_t>(GpuPass::Count) * 2u};

    //! Absolute query index for a frame slot, pass, and timestamp endpoint.
    [[nodiscard]] constexpr uint32_t timestampIndex(uint32_t frame_slot, GpuPass pass, bool is_end) const
    {
        return (frame_slot * TIMESTAMPS_PER_FRAME) + (static_cast<uint32_t>(pass) * 2u) + (is_end ? 1u : 0u);
    }

    LoggingLib::Logger& m_logger; //!< Logger reference (non-owning).
    vk::raii::QueryPool m_query_pool{nullptr}; //!< Timestamp query pool; null when disabled.
    vk::Device m_device_handle{nullptr}; //!< Plain device handle used for the readback.
    uint32_t m_frames_in_flight{0}; //!< Number of ping-pong slots in the pool.
    float m_timestamp_period_ns{0.0f}; //!< Nanoseconds per timestamp tick, from VkPhysicalDeviceLimits.
    uint64_t m_timestamp_mask{0}; //!< Mask of valid bits in the timestamp counter.
    bool m_enabled{false}; //!< False when the graphics queue lacks timestamp support.

    std::array<float, static_cast<size_t>(GpuPass::Count)> m_ema_ns{}; //!< Per-pass exponential moving average, in nanoseconds.
    bool m_ema_initialised{false}; //!< False until the first successful readback seeds the averages.

    //! Per-slot flag: a slot must have been submitted at least once before its results can be read.
    std::vector<uint8_t> m_slot_has_results;

    /*!
        Scratch buffer for the readback, sized for the eWithAvailability layout: each query
        yields a result value followed by a non-zero availability value. Held as a member so
        the readback allocates nothing per frame.
    */
    std::array<uint64_t, TIMESTAMPS_PER_FRAME * 2u> m_raw_results{};

    std::chrono::steady_clock::time_point m_last_log_time{}; //!< When the summary line was last emitted.
};
