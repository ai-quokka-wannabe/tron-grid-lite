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

/*
    Phase 2 — the compute ray tracer.

    Builds the world on the host, hands it to the GPU as a bounding volume hierarchy in storage
    buffers, and traces it in a compute shader. There is no rasteriser and no ray-tracing hardware:
    every pixel of the spectator window is a ray walked through the hierarchy by hand.

    The same tracer will render creature sensors, at a far smaller resolution, once bodies exist.
    Nothing here is a game — the camera exists so that a human can watch and debug.
*/

#include "camera.hpp"
#include "components.hpp"
#include "device.hpp"
#include "geometry.hpp"
#include "instance.hpp"
#include "profiler.hpp"
#include "spectator.hpp"
#include "surface.hpp"
#include "swapchain.hpp"
#include "tracer.hpp"
#include <bvh/bvh.hpp>
#include <log/logger.hpp>
#include <math/vector.hpp>
#include <window/window.hpp>
#include <window/window_event.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace
{

    //! Number of frames the host may record ahead of the GPU.
    constexpr uint32_t MAX_FRAMES_IN_FLIGHT{2u};

    /*!
        Ray segments per pixel.

        Two is the Phase 2 milestone: one primary ray, and one mirror bounce so that the neon
        appears in the floor. Phase 3 raises this once transmission splits the ray tree.
    */
    constexpr uint32_t MAX_BOUNCES{2u};

    //! Linear scale applied before the tone curve.
    constexpr float EXPOSURE{1.0f};

    //! Material slots in the world's material table.
    enum MaterialSlot : uint32_t {
        MATERIAL_FLOOR = 0u, //!< The mirror the whole world stands on.
        MATERIAL_NEON_PRIMARY = 1u, //!< Cyan tubes along ordinary grid lines.
        MATERIAL_NEON_ACCENT = 2u, //!< Orange tubes along major grid lines.
        MATERIAL_PILLAR = 3u //!< Standing blocks, bright enough to light the floor around them.
    };

    //! Converts a mesh into hierarchy triangles, tagging each with the given material.
    void appendTriangles(std::vector<BvhLib::Triangle>& out, const Mesh& mesh, uint32_t material)
    {
        for (size_t index{0u}; (index + 2u) < mesh.indices.size(); index += 3u) {
            const Vertex& a{mesh.vertices[mesh.indices[index]]};
            const Vertex& b{mesh.vertices[mesh.indices[index + 1u]]};
            const Vertex& c{mesh.vertices[mesh.indices[index + 2u]]};

            const MathLib::Vec3 v0{a.position[0], a.position[1], a.position[2]};
            const MathLib::Vec3 v1{b.position[0], b.position[1], b.position[2]};
            const MathLib::Vec3 v2{c.position[0], c.position[1], c.position[2]};

            out.push_back(BvhLib::Triangle{.v0 = v0, .material = material, .edge1 = v1 - v0, .padding0 = 0u, .edge2 = v2 - v0, .padding1 = 0u});
        }
    }

    /*!
        The world's material table.

        Three entries, all of them perfectly smooth. The floor carries no emission at all: every
        photon in this world starts inside a neon tube, and the floor is only ever as bright as
        what it reflects. Fresnel does the rest — barely anything head-on, everything at a grazing
        angle, which is what draws the long streaks towards the horizon.
    */
    [[nodiscard]] std::vector<Material> makeMaterials()
    {
        std::vector<Material> materials(4u);
        materials[MATERIAL_FLOOR] = makeMirror(MathLib::Vec3{0.85f, 0.90f, 1.00f});

        /*
            A high index of refraction, because it is the only reflectivity knob this material
            model has. Fresnel derives the head-on reflectance entirely from it: ordinary glass at
            1.5 returns four per cent, which is honest for a window and far too dim for a floor
            that is meant to read as a mirror. At 2.4 it returns about seventeen per cent head-on
            and still climbs to everything at a grazing angle, which is the effect the aesthetic
            is after. Nothing else in the world uses this value while transmission stays at zero.
        */
        materials[MATERIAL_FLOOR].index_of_refraction = 2.4f;
        materials[MATERIAL_NEON_PRIMARY] = makeEmissive(MathLib::Vec3{0.05f, 0.35f, 0.55f}, MathLib::Vec3{0.10f, 2.60f, 4.20f});
        materials[MATERIAL_NEON_ACCENT] = makeEmissive(MathLib::Vec3{0.55f, 0.25f, 0.05f}, MathLib::Vec3{4.40f, 1.60f, 0.15f});
        materials[MATERIAL_PILLAR] = makeEmissive(MathLib::Vec3{0.30f, 0.45f, 0.60f}, MathLib::Vec3{0.60f, 3.20f, 5.00f});
        return materials;
    }

    /*!
        A few blocks standing off the floor.

        Their purpose is not decoration. A perfectly flat world has nothing to reflect: the tubes
        lie a centimetre above the mirror, so their reflection sits a centimetre below and merges
        with the tube itself. Geometry with height is what makes the second ray segment visible,
        and it is the only way to see at a glance whether the mirror is working.
    */
    [[nodiscard]] Mesh makePillars()
    {
        constexpr float FLOOR_HALF_EXTENT{64.0f};

        Mesh pillars{};
        const std::array<MathLib::Vec3, 6u> positions{MathLib::Vec3{-24.0f, 0.0f, -18.0f}, MathLib::Vec3{18.0f, 0.0f, -30.0f}, MathLib::Vec3{34.0f, 0.0f, 6.0f},
            MathLib::Vec3{-38.0f, 0.0f, 14.0f}, MathLib::Vec3{6.0f, 0.0f, -52.0f}, MathLib::Vec3{-8.0f, 0.0f, 22.0f}};

        for (size_t index{0u}; index < positions.size(); ++index) {
            const float height{6.0f + (static_cast<float>(index % 3u) * 4.0f)};
            const MathLib::Vec3 half_extents{0.45f, height * 0.5f, 0.45f};
            const MathLib::Vec3 centre{positions[index].x, half_extents.y, positions[index].z};

            if ((std::abs(centre.x) < FLOOR_HALF_EXTENT) && (std::abs(centre.z) < FLOOR_HALF_EXTENT)) {
                pillars.append(generateBox(centre, half_extents));
            }
        }

        return pillars;
    }

} // namespace

int main()
{
    LoggingLib::Logger logger;

    try {
        logger.logInfo("TronGrid Lite - Phase 2 (a world for AI agents; this window is for the human observer only).");

        const WindowLib::WindowConfig window_config{.title = "TronGrid Lite - spectator", .width = 1280, .height = 720, .resizable = true, .decorated = true};
        const std::unique_ptr<WindowLib::Window> window{WindowLib::create(window_config, logger)};

#ifdef NDEBUG
        constexpr bool enable_validation{false};
#else
        constexpr bool enable_validation{true};
#endif

        const Instance instance{enable_validation, requiredSurfaceExtensions(), logger};
        const vk::raii::SurfaceKHR surface{createSurface(instance.get(), *window)};
        const Device device{instance, *surface, logger};

        Swapchain swapchain{device, *surface, window->width(), window->height(), logger};

        // The world: a flat mirror floor with neon tubes along its grid lines.
        const GridFloorConfig floor_config{.cells = 64u, .cell_size = 2.0f, .height = 0.0f};

        /*
            Thin tubes, sitting almost on the floor.

            The rasteriser of Phase 1 needed them wide and lifted, because a strip narrower than a
            pixel breaks into dashes and a coplanar strip fights the depth buffer. The tracer has
            neither problem: it samples geometry analytically and has no depth buffer at all, so
            the tubes can be the slender lines the aesthetic actually wants.
        */
        const NeonTubeConfig tube_config{.half_width = 0.025f, .surface_offset = 0.01f};

        const Mesh floor{generateGridFloor(floor_config)};
        const NeonGrid neon{generateGridFloorNeon(floor_config, tube_config)};

        const Mesh pillars{makePillars()};

        std::vector<BvhLib::Triangle> world_triangles;
        world_triangles.reserve(floor.triangleCount() + neon.primary.triangleCount() + neon.accent.triangleCount() + pillars.triangleCount());
        appendTriangles(world_triangles, floor, MATERIAL_FLOOR);
        appendTriangles(world_triangles, neon.primary, MATERIAL_NEON_PRIMARY);
        appendTriangles(world_triangles, neon.accent, MATERIAL_NEON_ACCENT);
        appendTriangles(world_triangles, pillars, MATERIAL_PILLAR);

        const std::chrono::steady_clock::time_point build_start{std::chrono::steady_clock::now()};
        const BvhLib::Bvh bvh{BvhLib::build(std::move(world_triangles))};
        const float build_milliseconds{std::chrono::duration<float, std::milli>{std::chrono::steady_clock::now() - build_start}.count()};

        logger.logInfo("Hierarchy built in " + std::to_string(static_cast<double>(build_milliseconds)) + " ms: " + std::to_string(bvh.triangles.size()) + " triangles, "
            + std::to_string(bvh.nodes.size()) + " nodes, depth " + std::to_string(bvh.depth()) + " of " + std::to_string(BvhLib::MAX_DEPTH) + ".");

        Tracer tracer{device, bvh, makeMaterials(), MAX_FRAMES_IN_FLIGHT, "trace.spv", logger};
        tracer.resize(swapchain.extent());

        const vk::CommandPoolCreateInfo pool_info{.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = device.graphicsFamilyIndex()};
        const vk::raii::CommandPool command_pool{device.get(), pool_info};

        const vk::CommandBufferAllocateInfo command_buffer_info{
            .commandPool = *command_pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = MAX_FRAMES_IN_FLIGHT};
        vk::raii::CommandBuffers command_buffers{device.get(), command_buffer_info};

        std::vector<vk::raii::Semaphore> image_available;
        std::vector<vk::raii::Fence> in_flight;
        for (uint32_t frame{0u}; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
            image_available.emplace_back(device.get(), vk::SemaphoreCreateInfo{});
            in_flight.emplace_back(device.get(), vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
        }

        // One present semaphore per swapchain image: a semaphore signalled for image N must not be
        // waited on while a different frame presents image M.
        std::vector<vk::raii::Semaphore> render_finished;
        for (uint32_t image{0u}; image < swapchain.imageCount(); ++image) {
            render_finished.emplace_back(device.get(), vk::SemaphoreCreateInfo{});
        }

        GpuProfiler profiler{device, MAX_FRAMES_IN_FLIGHT, logger};

        // The spectator: a free camera for the human observer. Creatures never use this.
        Camera camera{MathLib::Vec3{0.0f, 6.0f, 40.0f}};
        SpectatorController spectator;

        logger.logInfo("Phase 2 initialised on " + device.name() + " - fly with WASD, look with the mouse, Tab toggles cursor capture.");

        uint32_t frame_index{0u};
        bool needs_recreate{false};
        std::chrono::steady_clock::time_point previous_time{std::chrono::steady_clock::now()};

        while (!window->shouldClose()) {
            window->pumpEvents();

            WindowLib::WindowEvent event{};
            while (window->pollEvent(event)) {
                if (event.type == WindowLib::WindowEvent::Type::Resize) {
                    needs_recreate = true;
                } else if (event.type == WindowLib::WindowEvent::Type::Close) {
                    window->requestClose();
                }
                spectator.processEvent(event);
            }

            const std::chrono::steady_clock::time_point current_time{std::chrono::steady_clock::now()};
            const float delta_seconds{std::chrono::duration<float>{current_time - previous_time}.count()};
            previous_time = current_time;

            window->setCursorCaptured(spectator.cursorCaptured());
            spectator.update(camera, delta_seconds);

            // A minimised window has a zero-sized swapchain; there is nothing to render into.
            if ((window->width() == 0u) || (window->height() == 0u)) {
                window->waitEvents();
                continue;
            }

            if (needs_recreate) {
                device.get().waitIdle();
                swapchain.recreate(window->width(), window->height());
                tracer.resize(swapchain.extent());
                needs_recreate = false;
                continue;
            }

            const vk::raii::Fence& fence{in_flight[frame_index]};
            while (device.get().waitForFences({*fence}, vk::True, UINT64_MAX) == vk::Result::eTimeout) {
                // Retry — a timeout here is not an error.
            }

            uint32_t image_index{0u};
            try {
                const auto [acquire_result, acquired_index] = swapchain.get().acquireNextImage(UINT64_MAX, *image_available[frame_index], nullptr);
                if ((acquire_result == vk::Result::eErrorOutOfDateKHR) || (acquire_result == vk::Result::eSuboptimalKHR)) {
                    needs_recreate = true;
                    continue;
                }
                image_index = acquired_index;
            } catch (const vk::OutOfDateKHRError&) {
                needs_recreate = true;
                continue;
            }

            device.get().resetFences({*fence});
            profiler.collect(frame_index);
            profiler.logSummary();

            const vk::raii::CommandBuffer& command_buffer{command_buffers[frame_index]};
            command_buffer.reset();
            command_buffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
            profiler.resetFrame(command_buffer, frame_index);
            profiler.begin(command_buffer, frame_index, GpuPass::Frame);

            profiler.begin(command_buffer, frame_index, GpuPass::Trace);
            tracer.record(command_buffer, frame_index, camera, MAX_BOUNCES, EXPOSURE);
            profiler.end(command_buffer, frame_index, GpuPass::Trace);

            profiler.begin(command_buffer, frame_index, GpuPass::Present);

            /*
                The source stage must be the stage the acquire semaphore is waited on, not
                eTopOfPipe. A semaphore wait at eTransfer orders nothing against a barrier claiming
                to come from the top of the pipe, so the layout transition would be free to run
                before the image has actually been acquired.
            */
            const vk::ImageMemoryBarrier2 to_transfer_dst{.srcStageMask = vk::PipelineStageFlagBits2::eBlit,
                .srcAccessMask = vk::AccessFlagBits2::eNone,
                .dstStageMask = vk::PipelineStageFlagBits2::eBlit,
                .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                .image = swapchain.images()[image_index],
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &to_transfer_dst});

            /*
                A one-to-one blit rather than a copy, because the traced image is RGBA and the
                surface is BGRA. The blit performs that conversion; a copy would require identical
                formats and would swap red and blue.
            */
            const std::array<vk::Offset3D, 2> source_bounds{
                vk::Offset3D{0, 0, 0}, vk::Offset3D{static_cast<int32_t>(tracer.extent().width), static_cast<int32_t>(tracer.extent().height), 1}};
            const std::array<vk::Offset3D, 2> destination_bounds{
                vk::Offset3D{0, 0, 0}, vk::Offset3D{static_cast<int32_t>(swapchain.extent().width), static_cast<int32_t>(swapchain.extent().height), 1}};

            const vk::ImageBlit2 blit_region{.srcSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u},
                .srcOffsets = source_bounds,
                .dstSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u},
                .dstOffsets = destination_bounds};

            command_buffer.blitImage2(vk::BlitImageInfo2{.srcImage = tracer.outputImage(frame_index),
                .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
                .dstImage = swapchain.images()[image_index],
                .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
                .regionCount = 1u,
                .pRegions = &blit_region,
                .filter = vk::Filter::eNearest});

            const vk::ImageMemoryBarrier2 to_present{.srcStageMask = vk::PipelineStageFlagBits2::eBlit,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe,
                .dstAccessMask = vk::AccessFlagBits2::eNone,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = vk::ImageLayout::ePresentSrcKHR,
                .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                .image = swapchain.images()[image_index],
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u}};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &to_present});

            profiler.end(command_buffer, frame_index, GpuPass::Present);
            profiler.end(command_buffer, frame_index, GpuPass::Frame);
            command_buffer.end();

            const vk::PipelineStageFlags wait_stage{vk::PipelineStageFlagBits::eTransfer};
            const vk::SubmitInfo submit{.waitSemaphoreCount = 1u,
                .pWaitSemaphores = &*image_available[frame_index],
                .pWaitDstStageMask = &wait_stage,
                .commandBufferCount = 1u,
                .pCommandBuffers = &*command_buffer,
                .signalSemaphoreCount = 1u,
                .pSignalSemaphores = &*render_finished[image_index]};
            device.graphicsQueue().submit({submit}, *fence);

            try {
                const vk::PresentInfoKHR present{.waitSemaphoreCount = 1u,
                    .pWaitSemaphores = &*render_finished[image_index],
                    .swapchainCount = 1u,
                    .pSwapchains = &*swapchain.get(),
                    .pImageIndices = &image_index};
                const vk::Result result{device.presentQueue().presentKHR(present)};
                if (result == vk::Result::eSuboptimalKHR) {
                    needs_recreate = true;
                }
            } catch (const vk::OutOfDateKHRError&) {
                needs_recreate = true;
            }

            frame_index = (frame_index + 1u) % MAX_FRAMES_IN_FLIGHT;
        }

        device.get().waitIdle();
        logger.logInfo("Shutting down cleanly.");
    } catch (const std::exception& error) {
        logger.logFatal(std::string{"Fatal error: "} + error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
