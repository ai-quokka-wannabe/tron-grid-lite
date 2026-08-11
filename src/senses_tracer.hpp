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

#include "components.hpp"
#include "memory_arena.hpp"
#include "senses.hpp"
#include "vulkan_helpers.hpp"
#include "world.hpp"

#include <logging/logger.hpp>
#include <math/vector.hpp>

#include <cstdint>
#include <string>
#include <vector>

class Device; // forward declaration

/*!
    Answers radiance questions for creature senses: one `senses.slang` dispatch over a flat buffer
    of sample rays, submitted synchronously and read straight back.

    Synchronous deliberately. A tick hands every Program the senses of *this* tick, so the answer is
    needed in hand before the roster may proceed — the same reason `Acoustics::gather` is a plain
    function call. The dispatch is a handful of rays against a hierarchy the User's window walks
    millions of times per frame, so the round trip is dominated by submission overhead and the
    budget is untroubled.

    Eye samples and irradiance directions travel in the same buffer because they are the same
    question; the caller keeps track of which answer is which.
*/
class SensesTracer final : public RadianceSolver {
public:
    /*!
        \param device Logical device.
        \param world The Grid's geometry — nodes and triangles — already resident on the device.
        \param materials The optical material table, uploaded here for the shader to shade with.
        \param initial_instances The placements to trace against until `stage` replaces them, and
               the capacity every later staging is bounded by: the roster is fixed at startup, so
               the placement count never grows after construction. Must not be empty.
        \param max_rays Most sample rays one solve may carry. Sizes the two host-visible buffers.
        \param shader_path Path to `senses.spv`.
        \param logger Logger for the allocation report.
    */
    SensesTracer(const Device& device, const World& world, const std::vector<Material>& materials, const std::vector<BvhLib::InstanceRecord>& initial_instances,
        uint32_t max_rays, const std::string& shader_path, LoggingLib::Logger& logger);

    /*!
        Replaces the placements the next solves trace against.

        A memcpy into a host-visible buffer, because this happens per creature per tick: the world's
        own instance buffer stays the static upload the window renders from, and this pass owns the
        moving copy. Bounded by the construction-time count — a roster is fixed at startup, so more
        placements than were built for is a caller error and refused loudly.
    */
    void stage(const std::vector<BvhLib::InstanceRecord>& instances) override;

    /*!
        Traces one batch of sample rays and returns linear radiance per ray.

        \param rays Origin, direction interleaved: ray i is elements 2i and 2i + 1, w ignored.
        \param max_bounces Total ray segments per sample, at least 1.
        \return One radiance triple per ray, in ray order, in the w component nothing.
    */
    [[nodiscard]] std::vector<MathLib::Vec4> solve(const std::vector<MathLib::Vec4>& rays, uint32_t max_bounces) override;

private:
    const Device* m_device; //!< Non-owning.
    const World* m_world; //!< Non-owning.

    uint32_t m_max_rays; //!< Capacity of the ray and result buffers.
    uint32_t m_instance_capacity; //!< Placements the instance buffer was sized for.
    uint32_t m_instance_count; //!< Placements the next solve traces against.

    MemoryArena m_device_arena; //!< Holds the material table.
    MemoryArena m_host_arena; //!< Holds the mapped buffers below.

    VulkanHelpers::DeviceBuffer m_materials; //!< The optical material table on the device.

    vk::raii::Buffer m_rays{nullptr}; //!< Sample rays, written by the host each solve.
    vk::raii::Buffer m_results{nullptr}; //!< Radiance per ray, read by the host each solve.
    vk::raii::Buffer m_instances{nullptr}; //!< Placements, rewritten by `stage` as bodies move.
    void* m_rays_mapped{nullptr};
    void* m_results_mapped{nullptr};
    void* m_instances_mapped{nullptr};

    vk::raii::DescriptorSetLayout m_set_layout{nullptr};
    vk::raii::DescriptorPool m_descriptor_pool{nullptr};
    vk::raii::DescriptorSets m_descriptor_sets{nullptr};
    vk::raii::PipelineLayout m_pipeline_layout{nullptr};
    vk::raii::Pipeline m_pipeline{nullptr};

    vk::raii::CommandPool m_command_pool{nullptr};
    vk::raii::CommandBuffers m_command_buffers{nullptr};
    vk::raii::Fence m_fence{nullptr};
};
