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
#include "camera.hpp"
#include "components.hpp"
#include "memory_arena.hpp"
#include "vulkan_helpers.hpp"
#include "world.hpp"
#include <log/logger.hpp>
#include <cstdint>
#include <string>
#include <vector>

class Device; // forward declaration

/*!
    The renderer: a compute shader that walks a hand-built bounding volume hierarchy.

    There is no ray-tracing hardware in play. The hierarchy, the triangles and the materials live
    in ordinary storage buffers, and `grid_bvh.slang` traverses them with a fixed-size stack. Because
    every surface in the Grid is perfectly smooth, each intersection spawns exactly one reflected
    ray, so the whole thing is deterministic: no sampling, no variance, no denoiser.

    Each frame in flight owns its own output image, because the host records the next frame while
    the GPU may still be reading the previous one. What it writes is linear radiance rather than a
    picture: exposure, bloom, tone mapping and sRGB encoding all belong to the post-processing
    stage, which sees the whole image and the bloom pyramid together.
*/
class Tracer {
public:
    /*!
        Uploads the Grid and builds everything needed to trace it.

        \param device Logical device.
        \param world The Grid's geometry. Must outlive this tracer; an empty one renders black.
        \param materials Optical material table the triangles index into. Must not be empty.
        \param frames_in_flight Number of independent output images to allocate.
        \param shader_path Path to the compiled `trace.spv`, relative to the working directory.
        \param logger Logger for the upload summary.
    */
    Tracer(const Device& device, const World& world, const std::vector<Material>& materials, uint32_t frames_in_flight, const std::string& shader_path,
        LoggingLib::Logger& logger);

    // Non-copyable, non-movable: it owns Vulkan objects and is referenced by the frame loop.
    Tracer(const Tracer&) = delete;
    Tracer& operator=(const Tracer&) = delete;
    Tracer(Tracer&&) = delete;
    Tracer& operator=(Tracer&&) = delete;

    /*!
        Reallocates the output images at a new size and rewrites the descriptor sets.

        The caller must have waited for the device to be idle first, because the descriptor sets
        being rewritten may still be referenced by work in flight.
    */
    void resize(vk::Extent2D extent);

    /*!
        Records one dispatch.

        On return the output image for this slot is in `eGeneral` and holds linear radiance, ready
        for the post-processing stage to tone map. It is not a picture yet.

        \param command_buffer Command buffer to record into.
        \param frame_slot Frame in flight, which selects the output image and descriptor set.
        \param camera Viewpoint to trace from.
        \param max_bounces Depth of the ray tree, at least one.
    */
    void record(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot, const Camera& camera, uint32_t max_bounces) const;

    //! Returns the linear HDR image for a frame slot, valid until the next resize().
    [[nodiscard]] vk::Image outputImage(uint32_t frame_slot) const
    {
        return *m_output_images[frame_slot];
    }

    //! Returns one HDR view per frame in flight, for the post-processing stage to read.
    [[nodiscard]] std::vector<vk::ImageView> outputViews() const
    {
        std::vector<vk::ImageView> views;
        views.reserve(m_output_views.size());
        for (const vk::raii::ImageView& view : m_output_views) {
            views.push_back(*view);
        }
        return views;
    }

    //! Returns the size the output images were last allocated at.
    [[nodiscard]] vk::Extent2D extent() const
    {
        return m_extent;
    }

private:
    //! Allocates the per-frame output images and their views.
    void createOutputImages();

    //! Points every descriptor set at the current buffers and output images.
    void writeDescriptorSets();

    const Device* m_device{nullptr}; //!< Logical device (non-owning).
    const World* m_world{nullptr}; //!< The Grid's geometry (non-owning). Shared with the acoustic pass.
    LoggingLib::Logger* m_logger{nullptr}; //!< Logger (non-owning).

    uint32_t m_frames_in_flight{0u}; //!< Number of output images and descriptor sets.
    vk::Extent2D m_extent{}; //!< Current output image size.

    //! One block behind the material table. Declared before it so it outlives it.
    MemoryArena m_buffer_arena;

    VulkanHelpers::DeviceBuffer m_materials; //!< Optical material table, 32 bytes each.

    /*!
        One block of memory behind every output image, rather than one allocation each.

        Reset and refilled on every resize, which is exactly the shape a bump allocator wants: the
        images are created together and destroyed together, and nothing ever frees one and keeps its
        neighbour.
    */
    MemoryArena m_image_arena;

    std::vector<vk::raii::Image> m_output_images; //!< One per frame in flight.
    std::vector<vk::raii::ImageView> m_output_views; //!< Storage views of each output image.

    vk::raii::DescriptorSetLayout m_set_layout{nullptr}; //!< Layout of the single descriptor set.
    vk::raii::DescriptorPool m_descriptor_pool{nullptr}; //!< Pool the per-frame sets come from.
    vk::raii::DescriptorSets m_descriptor_sets{nullptr}; //!< One set per frame in flight.

    vk::raii::PipelineLayout m_pipeline_layout{nullptr}; //!< Layout carrying the push constants.
    vk::raii::Pipeline m_pipeline{nullptr}; //!< The compute pipeline itself.
};
