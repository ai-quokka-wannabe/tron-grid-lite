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

#include "profiler.hpp"
#include "device.hpp"
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    /*!
        Exponential moving average blend factor: ema = lerp(ema, sample, alpha). 0.05 is roughly
        a 20-frame effective average — smooth enough to read at a glance, quick enough to react
        to a cost change within about a third of a second at 60 fps.
    */
    constexpr float EMA_ALPHA{0.05f};

    //! How often the summary line is emitted. Once per second is live enough for tuning without flooding the log.
    constexpr std::chrono::seconds LOG_INTERVAL{1};

    //! Frame budget used to express headroom in the summary line. 16.67 ms is the 60 fps target.
    constexpr float FRAME_BUDGET_MS{1000.0f / 60.0f};

    /*!
        Pass-name strings for the summary line, indexed by GpuPass value. Kept short so the whole line
        fits one terminal row.

        **The size is deduced and then asserted, rather than declared from `GpuPass::Count`.** Written
        the other way round — `std::array<const char*, Count>` with five initialisers — adding a sixth
        enumerator is not an error at all: the array silently grows and the new slot is value
        initialised to `nullptr`, which reaches `std::string{nullptr}` in the summary line and is
        undefined behaviour rather than a wrong name. Deducing the size from the list and pinning it to
        the enum turns that into a compile error, which is what the enumerator's author wants to see.
    */
    constexpr std::array PASS_NAMES{"frame", "trace", "sensors", "post", "present"};
    static_assert(PASS_NAMES.size() == static_cast<size_t>(GpuPass::Count), "Every GpuPass enumerator needs a name in PASS_NAMES.");

    //! Formats a duration with two decimal places; std::to_string would emit six and make the line unreadable.
    [[nodiscard]] std::string formatMs(float milliseconds)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << milliseconds;
        return stream.str();
    }
}

GpuProfiler::GpuProfiler(const Device& device, uint32_t frames_in_flight, LoggingLib::Logger& logger) :
    m_logger(logger),
    m_device_handle(*device.get()),
    m_frames_in_flight(frames_in_flight)
{
    const vk::PhysicalDeviceProperties physical_properties{device.physicalDevice().getProperties()};
    m_timestamp_period_ns = physical_properties.limits.timestampPeriod;

    // A queue family reports how many bits of its timestamp counter are meaningful. Zero means
    // the queue cannot write timestamps at all, which is legal Vulkan even though every modern
    // desktop GPU supports them. Warn once and stay disabled rather than recording invalid queries.
    const std::vector<vk::QueueFamilyProperties> queue_families{device.physicalDevice().getQueueFamilyProperties()};
    const uint32_t graphics_family{device.graphicsFamilyIndex()};

    if (graphics_family >= static_cast<uint32_t>(queue_families.size())) {
        m_logger.logWarning("GPU profiler disabled: the graphics queue family index is out of range.");
        return;
    }

    const uint32_t timestamp_valid_bits{queue_families[static_cast<size_t>(graphics_family)].timestampValidBits};

    if ((timestamp_valid_bits == 0u) || (m_timestamp_period_ns <= 0.0f)) {
        m_logger.logWarning("GPU profiler disabled: the graphics queue family does not support timestamp queries.");
        return;
    }

    // Only the low timestamp_valid_bits of each counter are meaningful; the rest must be
    // cleared before subtracting so that wrap-around inside the valid range yields the correct
    // elapsed count instead of an enormous value. The branch avoids the undefined 1ULL << 64.
    m_timestamp_mask = (timestamp_valid_bits >= 64u) ? ~uint64_t{0u} : ((uint64_t{1u} << timestamp_valid_bits) - 1u);

    vk::QueryPoolCreateInfo pool_info{};
    pool_info.queryType = vk::QueryType::eTimestamp;
    pool_info.queryCount = TIMESTAMPS_PER_FRAME * m_frames_in_flight;
    m_query_pool = vk::raii::QueryPool{device.get(), pool_info};

    m_slot_has_results.assign(static_cast<size_t>(m_frames_in_flight), 0u);
    m_last_log_time = std::chrono::steady_clock::now();
    m_enabled = true;

    m_logger.logInfo("GPU profiler enabled: timestampPeriod " + formatMs(m_timestamp_period_ns) + " ns, " + std::to_string(timestamp_valid_bits) + " valid bits.");
}

void GpuProfiler::resetFrame(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot)
{
    if ((!m_enabled) || (frame_slot >= m_frames_in_flight)) {
        return;
    }

    // Every query must be reset before it is written, and the reset covers the whole slot so
    // that passes which are not recorded this frame stay unavailable rather than retaining a
    // stale value from an earlier frame.
    command_buffer.resetQueryPool(*m_query_pool, frame_slot * TIMESTAMPS_PER_FRAME, TIMESTAMPS_PER_FRAME);
    m_slot_has_results[static_cast<size_t>(frame_slot)] = 1u;
}

void GpuProfiler::begin(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot, GpuPass pass)
{
    if ((!m_enabled) || (frame_slot >= m_frames_in_flight) || (pass >= GpuPass::Count)) {
        return;
    }

    // eAllCommands means the timestamp is written once every preceding command has reached the
    // bottom of the pipeline, which is what makes the pair a wall-clock span of the pass rather
    // than of one pipeline stage.
    command_buffer.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *m_query_pool, timestampIndex(frame_slot, pass, false));
}

void GpuProfiler::end(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot, GpuPass pass)
{
    if ((!m_enabled) || (frame_slot >= m_frames_in_flight) || (pass >= GpuPass::Count)) {
        return;
    }

    command_buffer.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *m_query_pool, timestampIndex(frame_slot, pass, true));
}

void GpuProfiler::collect(uint32_t frame_slot)
{
    if ((!m_enabled) || (frame_slot >= m_frames_in_flight) || (m_slot_has_results[static_cast<size_t>(frame_slot)] == 0u)) {
        return;
    }

    /*
        The user-buffer overload of getQueryPoolResults fills our member array through a
        (void*, dataSize) pair; the other overload returns a std::vector and would therefore
        heap-allocate every single frame.

        eWithAvailability is deliberate and eWait is deliberately absent. The caller has
        already waited on this slot's fence, so every timestamp that was actually recorded has
        finished — asking the driver to wait as well would stall the GPU for no reason.
        Availability is requested per query because passes belonging to unimplemented phases
        never write their timestamps. Each query therefore yields two values: the result, then
        a non-zero availability marker.

        eNotReady is an expected, non-exceptional outcome and must NOT be treated as failure.
        The driver returns it whenever any query in the range is unavailable, regardless of
        eWithAvailability — the flag changes what is written into the buffer, not the return
        code. While Trace, Sensors and Post are unimplemented their queries are always
        unavailable, so this call returns eNotReady on every single frame. Bailing out here
        would discard the passes that did resolve and the profiler would never report anything
        at all. The per-query availability markers are written in both cases, so they are what
        decides which passes are usable.
    */
    constexpr vk::DeviceSize RESULT_STRIDE{sizeof(uint64_t) * 2u};
    const vk::Result query_result{m_device_handle.getQueryPoolResults(*m_query_pool, frame_slot * TIMESTAMPS_PER_FRAME, TIMESTAMPS_PER_FRAME, sizeof(m_raw_results),
        m_raw_results.data(), RESULT_STRIDE, vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability)};

    if ((query_result != vk::Result::eSuccess) && (query_result != vk::Result::eNotReady)) {
        return;
    }

    std::array<float, static_cast<size_t>(GpuPass::Count)> sample_ns{};
    for (uint32_t pass_index{0u}; pass_index < static_cast<uint32_t>(GpuPass::Count); ++pass_index) {
        const size_t start_offset{static_cast<size_t>(pass_index) * 4u};
        const size_t end_offset{start_offset + 2u};

        // A pass that recorded nothing this frame leaves both queries unavailable; report zero.
        const bool start_available{m_raw_results[start_offset + 1u] != 0u};
        const bool end_available{m_raw_results[end_offset + 1u] != 0u};
        if ((!start_available) || (!end_available)) {
            continue;
        }

        const uint64_t start_ticks{m_raw_results[start_offset] & m_timestamp_mask};
        const uint64_t end_ticks{m_raw_results[end_offset] & m_timestamp_mask};

        // Modular subtraction, then mask again, so a counter wrap between the two timestamps
        // still yields the correct elapsed tick count.
        const uint64_t delta_ticks{(end_ticks - start_ticks) & m_timestamp_mask};
        sample_ns[pass_index] = static_cast<float>(delta_ticks) * m_timestamp_period_ns;
    }

    if (!m_ema_initialised) {
        m_ema_ns = sample_ns;
        m_ema_initialised = true;
        return;
    }

    for (uint32_t pass_index{0u}; pass_index < static_cast<uint32_t>(GpuPass::Count); ++pass_index) {
        m_ema_ns[pass_index] = (m_ema_ns[pass_index] * (1.0f - EMA_ALPHA)) + (sample_ns[pass_index] * EMA_ALPHA);
    }
}

float GpuProfiler::averageMs(GpuPass pass) const
{
    if ((!m_enabled) || (pass >= GpuPass::Count)) {
        return 0.0f;
    }

    return m_ema_ns[static_cast<size_t>(pass)] / 1.0e6f;
}

void GpuProfiler::logSummary()
{
    if ((!m_enabled) || (!m_ema_initialised)) {
        return;
    }

    const std::chrono::steady_clock::time_point now{std::chrono::steady_clock::now()};
    if ((now - m_last_log_time) < LOG_INTERVAL) {
        return;
    }
    m_last_log_time = now;

    /*
        The line is built to be scanned rather than parsed: the whole-frame cost and the
        equivalent frame rate come first because they answer the question this renderer exists
        to answer, then the individual passes in execution order, then how much of a 60 fps
        budget is left. Passes that are not implemented yet read 0.00.
    */
    const float frame_ms{averageMs(GpuPass::Frame)};

    std::string line{"GPU " + formatMs(frame_ms) + " ms"};
    if (frame_ms > 0.0f) {
        line += " (" + formatMs(1000.0f / frame_ms) + " fps)";
    }
    line += " |";

    for (uint32_t pass_index{1u}; pass_index < static_cast<uint32_t>(GpuPass::Count); ++pass_index) {
        line += " " + std::string{PASS_NAMES[pass_index]} + " " + formatMs(averageMs(static_cast<GpuPass>(pass_index)));
    }

    line += " | budget " + formatMs((frame_ms / FRAME_BUDGET_MS) * 100.0f) + "% of 60 fps";
    m_logger.logInfo(line);
}
