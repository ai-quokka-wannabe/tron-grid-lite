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
    Phase 4 — the compute ray tracer and its post-processing.

    Builds the Grid on the host, hands it to the GPU as a bounding volume hierarchy in storage
    buffers, and traces it in a compute shader. There is no rasteriser and no ray-tracing hardware:
    every pixel of the User's window is a ray walked through the hierarchy by hand.

    The tracer writes linear radiance. Turning that into a picture — the bloom pyramid, the ACES
    tone curve, sRGB encoding — is the post-processing stage's job, and only then is the result
    blitted to the swapchain.

    The same tracer will render creature sensors, at a far smaller resolution, once bodies exist.
    Nothing here is a game — the camera exists so that the User can watch and debug.
*/

#include "camera.hpp"
#include "cinematic.hpp"
#include "components.hpp"
#include "device.hpp"
#include "geometry.hpp"
#include "instance.hpp"
#include "postprocess.hpp"
#include "profiler.hpp"
#include "spectator.hpp"
#include "surface.hpp"
#include "swapchain.hpp"
#include "tracer.hpp"
#include "vulkan_helpers.hpp"
#include "world.hpp"
#include <bvh/bvh.hpp>
#include <log/logger.hpp>
#include <math/vector.hpp>
#include <signal/signal.hpp>
#include <window/window.hpp>
#include <window/window_event.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace
{

    //! Number of frames the host may record ahead of the GPU.
    constexpr uint32_t MAX_FRAMES_IN_FLIGHT{2u};

    /*!
        What the event thread sends the render thread.

        Window events cannot be read from the render thread: on Win32 the message pump belongs to
        the thread that created the window, and on both platforms the event queue is filled by the
        platform's own callback. So the event thread translates and forwards, and this is the whole
        vocabulary of that channel.
    */
    struct RenderEvent {
        //! What kind of message this is.
        enum class Type {
            Resize, //!< The window changed size; the swapchain and its dependants must be rebuilt.
            Input, //!< A key, mouse or focus event for the User's camera.
            Stop //!< The window is closing; finish the current frame and return.
        };

        Type type{Type::Input}; //!< Discriminator.
        uint32_t width{0u}; //!< New width, for Resize.
        uint32_t height{0u}; //!< New height, for Resize.
        WindowLib::WindowEvent input{}; //!< The original event, for Input.
    };

    /*!
        Everything the two threads share, and the only thing they share.

        Collecting it here is what makes the boundary checkable: if a name is not a member of this
        struct, exactly one thread touches it, and no reasoning about locks is required. The queue
        carries work in one direction, the two flags carry single bits back in the other, and the
        mutex and condition variable exist solely so the render thread can sleep when there is
        nothing to draw.
    */
    struct RenderChannel {
        SignalsLib::Signal<RenderEvent> queue; //!< Event thread to render thread. Mutex-protected internally.
        std::mutex mutex; //!< Held across `emit` so a wakeup cannot be lost. See `send`.
        std::condition_variable cv; //!< Wakes the render thread when it is idling on a blank window.

        //! Render thread to event thread: what the User's Tab key asked for. Applied by whoever owns the window.
        std::atomic<bool> wants_cursor_capture{false};

        //! Render thread to event thread: the renderer has died, so stop pumping into a queue nobody drains.
        std::atomic<bool> render_failed{false};

        /*!
            Queues a message and wakes the render thread.

            The emit happens under the very mutex the render thread waits on, or the wakeup can be
            lost: a message queued after the waiter has tested its predicate but before it has
            registered as a waiter would notify nobody, and the render thread would sleep with work
            pending. LoggingLib::Logger documents the same hazard on its own queue.

            The notify is deliberately outside the lock, so the woken thread does not immediately
            block on the mutex that was just released.

            \param event Message to queue.
        */
        void send(const RenderEvent& event)
        {
            {
                const std::lock_guard<std::mutex> lock{mutex};
                queue.emit(event);
            }
            cv.notify_one();
        }
    };

    /*!
        Depth of the ray tree.

        A transmissive surface splits the ray, so this is how far a branch may descend rather than
        how many rays are traced: glass needs at least four to show what is behind it through both
        of its faces, and the reflection of that glass in the floor needs one more still.
    */
    constexpr uint32_t MAX_BOUNCES{6u};

    //! Linear scale applied to the traced radiance before the tone curve.
    constexpr float EXPOSURE{1.0f};

    //! Luminance below which a texel contributes nothing to the bloom.
    constexpr float BLOOM_THRESHOLD{1.0f};

    //! Multiplier on the bloom contribution. Zero skips the whole chain.
    constexpr float BLOOM_STRENGTH{0.5f};

    /*!
        Radial darkening of the corners.

        Enabled for the User's window, where it reads as photographic. It must stay at zero for
        creature sensors: a synthetic brightness gradient is exactly the kind of artefact a Program
        would learn to exploit instead of learning the Grid.
    */
    constexpr float VIGNETTE_STRENGTH{0.35f};

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
        Returns the directory holding this executable.

        The compiled shaders sit beside the binary, and they used to be opened by bare relative
        name — which resolves against the *working* directory, not the executable's. That works
        only when the program happens to be launched from its own output directory, and silently
        fails everywhere else: every IDE debug configuration, every shortcut, and every user who
        unpacks a release and runs it from anywhere but inside the folder.

        Falls back to the working directory if the platform call fails, which restores exactly the
        old behaviour rather than making a bad situation worse.
    */
    [[nodiscard]] std::filesystem::path executableDirectory()
    {
#ifdef _WIN32
        std::wstring buffer(MAX_PATH, wchar_t{});
        for (;;) {
            const DWORD written{GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()))};
            if (written == 0u) {
                return std::filesystem::current_path();
            }

            // Truncation is reported by filling the buffer exactly, so grow and ask again.
            if (written < buffer.size()) {
                buffer.resize(written);
                break;
            }

            buffer.resize(buffer.size() * 2u);
        }

        return std::filesystem::path{buffer}.parent_path();
#else
        std::string buffer(PATH_MAX, char{});
        const ssize_t written{readlink("/proc/self/exe", buffer.data(), buffer.size())};
        if (written <= 0) {
            return std::filesystem::current_path();
        }

        buffer.resize(static_cast<size_t>(written));
        return std::filesystem::path{buffer}.parent_path();
#endif
    }

    /*!
        The Grid's material table.

        Three entries, all of them perfectly smooth. The floor carries no emission at all: every
        photon in the Grid starts inside a neon tube, and the floor is only ever as bright as
        what it reflects. Fresnel does the rest — barely anything head-on, everything at a grazing
        angle, which is what draws the long streaks towards the horizon.
    */
    [[nodiscard]] std::vector<Material> makeMaterials()
    {
        std::vector<Material> materials(6u);
        materials[MATERIAL_FLOOR] = makeMirror(MathLib::Vec3{0.85f, 0.90f, 1.00f});

        /*
            A high index of refraction, because it is the only reflectivity knob this material
            model has. Fresnel derives the head-on reflectance entirely from it: ordinary glass at
            1.5 returns four per cent, which is honest for a window and far too dim for a floor
            that is meant to read as a mirror. At 2.4 it returns about seventeen per cent head-on
            and still climbs to everything at a grazing angle, which is the effect the aesthetic
            is after. Nothing else in the Grid uses this value while transmission stays at zero.
        */
        materials[MATERIAL_FLOOR].index_of_refraction = 2.4f;
        materials[MATERIAL_NEON_PRIMARY] = makeEmissive(MathLib::Vec3{0.05f, 0.35f, 0.55f}, MathLib::Vec3{0.10f, 2.60f, 4.20f});
        materials[MATERIAL_NEON_ACCENT] = makeEmissive(MathLib::Vec3{0.55f, 0.25f, 0.05f}, MathLib::Vec3{4.40f, 1.60f, 0.15f});
        materials[MATERIAL_PILLAR] = makeEmissive(MathLib::Vec3{0.30f, 0.45f, 0.60f}, MathLib::Vec3{0.60f, 3.20f, 5.00f});

        // Ordinary glass. The faint tint is what the transmitted ray picks up crossing it, so a
        // thicker slab does not darken more than a thin one — Beer-Lambert absorption would need
        // the path length through the medium, which is a later refinement.
        materials[MATERIAL_GLASS] = makeGlass(MathLib::Vec3{0.80f, 0.92f, 0.95f}, 1.52f);

        // The case the old three-type material model could not express at all: a tube that emits
        // and transmits at the same time, which is what a neon tube with a glass envelope is.
        materials[MATERIAL_GLOWING_GLASS] = makeGlowingGlass(MathLib::Vec3{0.90f, 0.70f, 0.95f}, MathLib::Vec3{1.60f, 0.30f, 2.20f}, 1.46f, 0.85f);
        return materials;
    }

    /*!
        A few blocks standing off the floor.

        Their purpose is not decoration. A perfectly flat Grid has nothing to reflect: the tubes
        lie a centimetre above the mirror, so their reflection sits a centimetre below and merges
        with the tube itself. Geometry with height is what makes the second ray segment visible,
        and it is the only way to see at a glance whether the mirror is working.
    */
    /*!
        Plants a box on the floor: returns the centre that puts its base on the ground beneath it.

        Everything standing in the Grid goes through this. The floor is no longer a plane, so a
        fixed height would leave objects buried in a terrace or hovering over a hollow, and the
        error is worst exactly where the relief is most interesting.
    */
    [[nodiscard]] MathLib::Vec3 plantOnFloor(float world_x, float world_z, const MathLib::Vec3& half_extents, const GridFloorConfig& floor_config)
    {
        /*
            The ground under a box is not one height: a terrace step can run straight through its
            footprint, and the wider the box the likelier that is. Sitting the base on the LOWEST
            point of the footprint buries part of the box rather than leaving the rest of it
            hovering — something set into the ground reads as deliberate, whereas something floating
            above it reads as broken.

            The samples ask `gridMeshHeight` rather than `gridSurfaceHeight`, because the mesh ramps
            across a cell where the analytic surface steps. Asking the analytic function left the
            glowing column standing 0.29 m clear of its own reflection.

            The drawn floor is piecewise linear, so the lowest point over a rectangle is always at a
            corner of that rectangle or at a grid vertex inside it. Both are sampled.
        */
        const float min_x{world_x - half_extents.x};
        const float max_x{world_x + half_extents.x};
        const float min_z{world_z - half_extents.z};
        const float max_z{world_z + half_extents.z};

        float ground{gridMeshHeight(world_x, world_z, floor_config)};

        for (const float sample_x : {min_x, max_x}) {
            for (const float sample_z : {min_z, max_z}) {
                ground = std::min(ground, gridMeshHeight(sample_x, sample_z, floor_config));
            }
        }

        const float half_size{(static_cast<float>(floor_config.cells) * floor_config.cell_size) * 0.5f};
        const auto firstVertexAbove = [&](float world_coordinate) {
            return static_cast<int32_t>(std::ceil((world_coordinate + half_size) / floor_config.cell_size));
        };

        for (int32_t vertex_x{firstVertexAbove(min_x)}; (static_cast<float>(vertex_x) * floor_config.cell_size) - half_size <= max_x; ++vertex_x) {
            for (int32_t vertex_z{firstVertexAbove(min_z)}; (static_cast<float>(vertex_z) * floor_config.cell_size) - half_size <= max_z; ++vertex_z) {
                const float vertex_world_x{(static_cast<float>(vertex_x) * floor_config.cell_size) - half_size};
                const float vertex_world_z{(static_cast<float>(vertex_z) * floor_config.cell_size) - half_size};
                ground = std::min(ground, gridMeshHeight(vertex_world_x, vertex_world_z, floor_config));
            }
        }

        return MathLib::Vec3{world_x, ground + half_extents.y, world_z};
    }

    [[nodiscard]] Mesh makePillars(const GridFloorConfig& floor_config)
    {
        constexpr float FLOOR_HALF_EXTENT{64.0f};

        Mesh pillars{};
        const std::array<MathLib::Vec3, 6u> positions{MathLib::Vec3{-24.0f, 0.0f, -18.0f}, MathLib::Vec3{18.0f, 0.0f, -30.0f}, MathLib::Vec3{34.0f, 0.0f, 6.0f},
            MathLib::Vec3{-38.0f, 0.0f, 14.0f}, MathLib::Vec3{6.0f, 0.0f, -52.0f}, MathLib::Vec3{-8.0f, 0.0f, 22.0f}};

        for (size_t index{0u}; index < positions.size(); ++index) {
            const float height{6.0f + (static_cast<float>(index % 3u) * 4.0f)};
            const MathLib::Vec3 half_extents{0.45f, height * 0.5f, 0.45f};
            const MathLib::Vec3 centre{plantOnFloor(positions[index].x, positions[index].z, half_extents, floor_config)};

            if ((std::abs(centre.x) < FLOOR_HALF_EXTENT) && (std::abs(centre.z) < FLOOR_HALF_EXTENT)) {
                pillars.append(generateBox(centre, half_extents));
            }
        }

        return pillars;
    }

    /*!
        Glass standing in front of the emissive pillars.

        Placed deliberately between the camera's opening view and the lit geometry, because a
        refracting surface is only legible when there is something recognisable behind it to bend.
        Each slab is a solid box rather than a plane: a ray must cross two interfaces to pass
        through, which is what makes the refraction visible instead of merely a tint.
    */
    [[nodiscard]] Mesh makeGlassSlabs(const GridFloorConfig& floor_config)
    {
        Mesh slabs{};

        // Broad upright panes, thin front to back, spread across the near view.
        const MathLib::Vec3 wide_slab{3.0f, 3.0f, 0.35f};
        slabs.append(generateBox(plantOnFloor(-9.0f, 8.0f, wide_slab, floor_config), wide_slab));
        const MathLib::Vec3 mid_slab{2.2f, 2.4f, 0.35f};
        slabs.append(generateBox(plantOnFloor(2.0f, 2.0f, mid_slab, floor_config), mid_slab));
        const MathLib::Vec3 tall_slab{2.6f, 3.6f, 0.35f};
        slabs.append(generateBox(plantOnFloor(13.0f, 10.0f, tall_slab, floor_config), tall_slab));

        return slabs;
    }

    //! A glowing translucent column: emission and transmission in the same surface.
    [[nodiscard]] Mesh makeGlowingColumn(const GridFloorConfig& floor_config)
    {
        const MathLib::Vec3 column{0.7f, 5.0f, 0.7f};
        return generateBox(plantOnFloor(-2.0f, -12.0f, column, floor_config), column);
    }

    /*!
        Writes one frame to a binary PPM file.

        PPM because it needs no library at all: a nine-byte header and then the pixels. This
        project writes its own subsystems rather than taking dependencies, and an image format the
        encoder in tools/ can read is not worth making an exception for. The alpha channel is
        dropped on the way out.
    */
    void writePpm(const std::filesystem::path& path, const uint8_t* rgba, uint32_t width, uint32_t height)
    {
        std::ofstream file{path, std::ios::binary};
        if (!file.is_open()) {
            throw std::runtime_error{"Cannot open frame file for writing: " + path.string()};
        }

        file << "P6\n" << width << " " << height << "\n255\n";

        std::vector<uint8_t> row(static_cast<size_t>(width) * 3u);
        for (uint32_t y{0u}; y < height; ++y) {
            const uint8_t* source{rgba + (static_cast<size_t>(y) * width * 4u)};
            for (uint32_t x{0u}; x < width; ++x) {
                row[(static_cast<size_t>(x) * 3u) + 0u] = source[(static_cast<size_t>(x) * 4u) + 0u];
                row[(static_cast<size_t>(x) * 3u) + 1u] = source[(static_cast<size_t>(x) * 4u) + 1u];
                row[(static_cast<size_t>(x) * 3u) + 2u] = source[(static_cast<size_t>(x) * 4u) + 2u];
            }
            file.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        }

        /*
            Closed here rather than left to the destructor: the destructor flushes whatever is still
            buffered and then has nowhere to report that the flush failed, so a disk filling on the
            last few kilobytes would truncate the frame and still look like success.
        */
        file.close();
        if (!file) {
            throw std::runtime_error{"Failed while writing frame file: " + path.string()};
        }
    }

} // namespace

/*!
    Renders a closed camera loop to a sequence of image files and returns.

    Deliberately unlike the interactive loop: no swapchain, no presentation, no frames in flight
    and no attempt at real time. Each frame is submitted on its own and waited on before the next,
    which makes the output identical on every run and on every machine — a recording that flickers
    differently each time it is made is not a recording.

    The readback here is the same operation a creature sensor will need in Phase 6: render into an
    image the size of an eye, then copy it back so that something outside the GPU can read it.
*/
int recordCinematic(const Device& device, Tracer& tracer, PostProcess& post_process, LoggingLib::Logger& logger, uint32_t width, uint32_t height, uint32_t frame_count,
    const std::filesystem::path& output_directory)
{
    // An extent of zero, or one past what the device can make, is a valid-usage violation the driver
    // may do anything with — and a release build has no validation layers to say so. Same guard as
    // the swapchain already applies for a minimised window.
    const uint32_t max_extent{device.physicalDevice().getProperties().limits.maxImageDimension2D};
    if ((width == 0u) || (height == 0u) || (width > max_extent) || (height > max_extent)) {
        throw std::runtime_error{"Recording size must be between 1x1 and " + std::to_string(max_extent) + "x" + std::to_string(max_extent) + "."};
    }

    const vk::Extent2D extent{width, height};
    tracer.resize(extent);
    post_process.resize(extent, tracer.outputViews());

    std::filesystem::create_directories(output_directory);

    const vk::DeviceSize frame_bytes{static_cast<vk::DeviceSize>(width) * height * 4u};

    const vk::raii::Buffer readback{
        device.get(), vk::BufferCreateInfo{.size = frame_bytes, .usage = vk::BufferUsageFlagBits::eTransferDst, .sharingMode = vk::SharingMode::eExclusive}};
    const vk::MemoryRequirements requirements{readback.getMemoryRequirements()};
    const vk::raii::DeviceMemory readback_memory{device.get(),
        vk::MemoryAllocateInfo{.allocationSize = requirements.size,
            .memoryTypeIndex = VulkanHelpers::findMemoryType(device.physicalDevice(), requirements.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)}};
    readback.bindMemory(*readback_memory, 0u);

    const vk::raii::CommandPool command_pool{
        device.get(), vk::CommandPoolCreateInfo{.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = device.graphicsFamilyIndex()}};
    vk::raii::CommandBuffers command_buffers{
        device.get(), vk::CommandBufferAllocateInfo{.commandPool = *command_pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1u}};
    const vk::raii::CommandBuffer& command_buffer{command_buffers.front()};

    const vk::raii::Fence fence{device.get(), vk::FenceCreateInfo{}};

    const CinematicPath path;
    Camera camera{};

    logger.logInfo("Recording " + std::to_string(frame_count) + " frames at " + std::to_string(width) + "x" + std::to_string(height) + " into "
        + output_directory.string() + ".");

    for (uint32_t frame{0u}; frame < frame_count; ++frame) {
        // The loop closes on itself, so the last frame must stop just short of the first rather
        // than repeating it — dividing by the count, not by the count less one, does that.
        path.apply(camera, static_cast<float>(frame) / static_cast<float>(frame_count));

        command_buffer.reset();
        command_buffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

        tracer.record(command_buffer, 0u, camera, MAX_BOUNCES);
        post_process.record(command_buffer, 0u, BLOOM_THRESHOLD, BLOOM_STRENGTH, VIGNETTE_STRENGTH, EXPOSURE);

        // The post-processing stage leaves its output in eTransferSrcOptimal, which is exactly what
        // a copy to a buffer wants.
        const vk::BufferImageCopy2 region{.bufferOffset = 0u,
            .bufferRowLength = 0u,
            .bufferImageHeight = 0u,
            .imageSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u},
            .imageOffset = vk::Offset3D{0, 0, 0},
            .imageExtent = vk::Extent3D{width, height, 1u}};
        command_buffer.copyImageToBuffer2(vk::CopyImageToBufferInfo2{.srcImage = post_process.outputImage(0u),
            .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
            .dstBuffer = *readback,
            .regionCount = 1u,
            .pRegions = &region});

        const vk::MemoryBarrier2 copy_visible{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eHost,
            .dstAccessMask = vk::AccessFlagBits2::eHostRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1u, .pMemoryBarriers = &copy_visible});

        command_buffer.end();

        device.get().resetFences({*fence});
        device.graphicsQueue().submit({vk::SubmitInfo{.commandBufferCount = 1u, .pCommandBuffers = &*command_buffer}}, *fence);
        while (device.get().waitForFences({*fence}, vk::True, UINT64_MAX) == vk::Result::eTimeout) {
            // Retry — a timeout here is not an error.
        }

        const void* mapped{readback_memory.mapMemory(0u, frame_bytes)};
        std::string name{std::to_string(frame)};
        name.insert(0u, 5u - std::min<size_t>(name.size(), 5u), '0');
        writePpm(output_directory / ("frame_" + name + ".ppm"), static_cast<const uint8_t*>(mapped), width, height);
        readback_memory.unmapMemory();

        if (((frame + 1u) % 25u) == 0u) {
            logger.logInfo("Recorded " + std::to_string(frame + 1u) + " of " + std::to_string(frame_count) + " frames.");
        }
    }

    device.get().waitIdle();
    logger.logInfo("Recording complete.");
    return EXIT_SUCCESS;
}

/*!
    Draws the Grid until told to stop. Runs on the render thread and owns the Vulkan timeline.

    Every Vulkan object created here belongs to this thread for its whole life, which is the point:
    a queue submission wants one owner, and the rule "if it is Vulkan, it lives on this stack" is
    one anybody can check by reading. The window is deliberately not a parameter — this function
    cannot ask how big the window is, only be told, because asking would race the thread that owns
    it.

    \param device Logical device, queues and physical device.
    \param swapchain Presentation chain. Recreated here on every resize.
    \param tracer The compute ray tracer.
    \param post_process Bloom, tone mapping and sRGB encoding.
    \param logger Thread-safe logger.
    \param channel The only state shared with the event thread.
*/
void runRenderLoop(const Device& device, Swapchain& swapchain, Tracer& tracer, PostProcess& post_process, LoggingLib::Logger& logger, RenderChannel& channel)
{
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

    /*
        Everything above this point is destroyed when this scope unwinds, and a great deal of it may
        still be in use by a submission the GPU has not finished. On the ordinary path the loop below
        exits and the device is idled first — but an exception escaping the loop skipped that
        entirely. `vk::OutOfDateKHRError` is caught inside, yet surface-lost, device-lost and
        out-of-device-memory are not, and any of them would run destructors for the command pool, the
        semaphores, the query pool and every image while work was still executing.

        The guard makes the wait unconditional. Its own failure is swallowed deliberately: a device
        that is already lost cannot be waited on, and throwing from a destructor during unwinding
        would replace the real error with a call to std::terminate.
    */
    struct DeviceIdleGuard {
        const Device* device;

        ~DeviceIdleGuard()
        {
            try {
                device->get().waitIdle();
            }
            // NOLINTNEXTLINE(bugprone-empty-catch) — see above: this runs during unwinding.
            catch (...) {
                // Deliberately swallowed. Throwing from a destructor while an exception is in
                // flight calls std::terminate and destroys the diagnostic that matters.
            }
        }
    } idle_guard{&device};

    // The debug camera: the User's free-flight view. Creatures never use this.
    Camera camera{MathLib::Vec3{0.0f, 6.0f, 40.0f}};
    SpectatorController spectator;

    logger.logInfo("Phase 4 initialised on " + device.name() + " - fly with WASD, look with the mouse, Tab toggles cursor capture.");

    uint32_t frame_index{0u};
    bool needs_recreate{false};
    bool stopping{false};

    // This thread's own idea of the window size, updated only by Resize messages. It never asks the
    // window directly: that would race the event thread.
    uint32_t surface_width{swapchain.extent().width};
    uint32_t surface_height{swapchain.extent().height};

    /*
        Everything the picture depends on.

        A frame is drawn only when one of these differs from what the last frame was drawn from. The
        comparison is against the *state*, not against a set of dirty flags raised by whoever
        modified it, and that is deliberate: a dirty flag is correct only if every writer remembers
        to raise it, whereas comparing the state cannot be forgotten. Wrongly deciding a frame is
        needed costs one redundant frame; wrongly deciding it is not costs a window that has stopped
        updating, so the cheap-to-get-right version is the one to have.

        The Grid itself is absent from this because it cannot yet change — `World` is uploaded once
        and is immutable. Phase 6 is where a generation counter joins this struct, because that is
        when creatures move.
    */
    struct ViewState {
        MathLib::Vec3 position{};
        MathLib::Quat orientation{};
        float fov_y{0.0f};
        uint32_t width{0u};
        uint32_t height{0u};

        [[nodiscard]] bool operator==(const ViewState&) const = default;
    };

    ViewState presented{};
    bool has_presented{false};

    std::chrono::steady_clock::time_point previous_time{std::chrono::steady_clock::now()};

    while (!stopping) {
        // Drain everything queued since the last frame. Input is integrated rather than sampled, so
        // no event may be dropped, however many arrived during one frame.
        RenderEvent message{};
        while (channel.queue.consume(message)) {
            switch (message.type) {
            case RenderEvent::Type::Resize:
                surface_width = message.width;
                surface_height = message.height;
                needs_recreate = true;
                break;

            case RenderEvent::Type::Input:
                spectator.processEvent(message.input);
                break;

            case RenderEvent::Type::Stop:
                stopping = true;
                break;
            }
        }

        if (stopping) {
            break;
        }

        const std::chrono::steady_clock::time_point current_time{std::chrono::steady_clock::now()};
        const float delta_seconds{std::chrono::duration<float>{current_time - previous_time}.count()};
        previous_time = current_time;

        spectator.update(camera, delta_seconds);

        // Published for the event thread to apply, because ShowCursor is per-thread on Win32.
        channel.wants_cursor_capture = spectator.cursorCaptured();

        /*
            Sleep until there is a reason to draw.

            This is what a CAD viewport does and what a game loop does not, and the Grid is far
            closer to the former: it is a world that mostly sits still, watched through a window
            that is usually not moving. Spinning at the GPU's maximum rate to redraw an identical
            image is the single most expensive thing this program can do for no result — it holds a
            laptop GPU at full clock, with the fans and the battery drain that implies, to produce a
            picture nobody can distinguish from the previous one.

            The wait is the same condition variable the minimised case uses, because the wake-up
            condition is identical: something arrived. A held key produces motion every frame and so
            keeps the loop running; releasing it stops the motion, the state stops changing, and the
            renderer goes quiet on its own without anybody having to say so.

            That last property is also how the GPU profiler is fed: it averages over frames, so
            holding a movement key is what produces a run of them to average. There is deliberately
            no flag to draw unconditionally — it would exist solely to do what holding W already
            does.
        */
        const ViewState wanted{
            .position = camera.position(), .orientation = camera.orientation(), .fov_y = camera.fovY(), .width = surface_width, .height = surface_height};

        const bool blank{(surface_width == 0u) || (surface_height == 0u)};
        const bool idle{has_presented && (wanted == presented) && !needs_recreate};

        if (blank || idle) {
            {
                std::unique_lock<std::mutex> lock{channel.mutex};
                channel.cv.wait(lock, [&channel]() {
                    return !channel.queue.empty();
                });
            }

            /*
                The clock restarts here, and it is not optional. `delta_seconds` is measured from
                the previous pass through the loop, so without this a thread that slept for a minute
                would wake, compute a delta of sixty seconds, and hand it to the spectator — which
                integrates velocity against it and would fling the camera a kilometre on the first
                keystroke after an idle.
            */
            previous_time = std::chrono::steady_clock::now();
            continue;
        }

        if (needs_recreate) {
            // Swapchain::recreate idles the device itself before rebuilding, which is also what the
            // two resizes below require, so there is deliberately no waitIdle here: two calls would
            // leave it ambiguous which layer owns the invariant.
            swapchain.recreate(surface_width, surface_height);

            /*
                The semaphores must be rebuilt with the swapchain. There is one per image and they
                are indexed by the acquired image index, but vkGetSwapchainImagesKHR may legally
                return more images than were requested, and nothing requires two swapchains built
                from the same surface to return the same count. A grown swapchain would index past
                the end of this vector and hand the driver a garbage handle — before any validation
                layer could see it.
            */
            render_finished.clear();
            for (uint32_t image{0u}; image < swapchain.imageCount(); ++image) {
                render_finished.emplace_back(device.get(), vk::SemaphoreCreateInfo{});
            }

            tracer.resize(swapchain.extent());
            post_process.resize(swapchain.extent(), tracer.outputViews());
            needs_recreate = false;

            /*
                A rebuilt swapchain's images have never been drawn to, and their contents are
                undefined. Forgetting this is how on-demand rendering shows garbage: a resize that
                happens to leave the width, the height and the camera exactly as they were would
                otherwise look idle on the very next pass, and the window would sit displaying
                whatever the driver handed back until the User moved something.
            */
            has_presented = false;
            continue;
        }

        const vk::raii::Fence& fence{in_flight[frame_index]};
        while (device.get().waitForFences({*fence}, vk::True, UINT64_MAX) == vk::Result::eTimeout) {
            // Retry — a timeout here is not an error.
        }

        uint32_t image_index{0u};
        bool acquire_suboptimal{false};
        try {
            /*
                vulkan-hpp throws for an out-of-date swapchain and treats a suboptimal one as
                success, so eSuboptimalKHR is the only non-success code that can reach here — and it
                means the image WAS acquired and this semaphore will be signalled.

                Abandoning the frame at that point would leave it signalled with nothing to consume
                it, and the same semaphore is handed to the next acquire for this slot, which
                VUID-vkAcquireNextImageKHR-semaphore-01779 forbids. A suboptimal image is still a
                perfectly usable image, so it is rendered and presented as normal and the swapchain
                is rebuilt afterwards. Dragging a window edge produces this constantly, because the
                size is sampled once when the swapchain is built.
            */
            const auto [acquire_result, acquired_index] = swapchain.get().acquireNextImage(UINT64_MAX, *image_available[frame_index], nullptr);
            acquire_suboptimal = (acquire_result == vk::Result::eSuboptimalKHR);
            image_index = acquired_index;
        } catch (const vk::OutOfDateKHRError&) {
            // A failed acquire signals nothing, so the semaphore may be reused unchanged.
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
        tracer.record(command_buffer, frame_index, camera, MAX_BOUNCES);
        profiler.end(command_buffer, frame_index, GpuPass::Trace);

        profiler.begin(command_buffer, frame_index, GpuPass::Post);
        post_process.record(command_buffer, frame_index, BLOOM_THRESHOLD, BLOOM_STRENGTH, VIGNETTE_STRENGTH, EXPOSURE);
        profiler.end(command_buffer, frame_index, GpuPass::Post);

        profiler.begin(command_buffer, frame_index, GpuPass::Present);

        /*
            The source stage must be the stage the acquire semaphore is waited on, not eTopOfPipe. A
            semaphore wait at eTransfer orders nothing against a barrier claiming to come from the
            top of the pipe, so the layout transition would be free to run before the image has
            actually been acquired.
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
            A one-to-one blit rather than a copy, because the traced image is RGBA and the surface is
            BGRA. The blit performs that conversion; a copy would require identical formats and would
            swap red and blue.
        */
        const std::array<vk::Offset3D, 2> source_bounds{
            vk::Offset3D{0, 0, 0}, vk::Offset3D{static_cast<int32_t>(post_process.extent().width), static_cast<int32_t>(post_process.extent().height), 1}};
        const std::array<vk::Offset3D, 2> destination_bounds{
            vk::Offset3D{0, 0, 0}, vk::Offset3D{static_cast<int32_t>(swapchain.extent().width), static_cast<int32_t>(swapchain.extent().height), 1}};

        const vk::ImageBlit2 blit_region{.srcSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u},
            .srcOffsets = source_bounds,
            .dstSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u},
            .dstOffsets = destination_bounds};

        command_buffer.blitImage2(vk::BlitImageInfo2{.srcImage = post_process.outputImage(frame_index),
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
            if ((result == vk::Result::eSuboptimalKHR) || acquire_suboptimal) {
                needs_recreate = true;
            }
        } catch (const vk::OutOfDateKHRError&) {
            needs_recreate = true;
        }

        // Recorded only after the frame has actually been submitted, so that an early `continue` on
        // an out-of-date swapchain leaves the loop believing it still owes a picture — which it does.
        presented = wanted;
        has_presented = true;

        frame_index = (frame_index + 1u) % MAX_FRAMES_IN_FLIGHT;
    }
}

int main(int argc, char** argv)
{
    LoggingLib::Logger logger;

    try {
        logger.logInfo("TronGrid Lite - Phase 4 (the Grid, a world for Programs; this window is for the User only).");

        /*
            Recording mode. The window is still created because the device picks its present queue
            against a surface, but nothing is ever presented to it.
        */
        bool recording{false};
        uint32_t record_width{1280u};
        uint32_t record_height{720u};
        uint32_t record_frames{240u};
        std::filesystem::path record_directory{"frames"};

        for (int index{1}; index < argc; ++index) {
            const std::string argument{argv[index]};
            /*
                std::from_chars rather than std::stoul: stoul is specified in terms of strtoul,
                which negates a leading minus into the unsigned result rather than failing, so
                "--frames -1" would quietly become 4294967295 frames. Parsing straight into the
                uint32_t makes a negative, a non-number and an out-of-range value the same error.
            */
            const auto value = [&](uint32_t fallback) -> uint32_t {
                if ((index + 1) >= argc) {
                    return fallback;
                }

                const char* const text{argv[++index]};
                const char* const text_end{text + std::strlen(text)};
                uint32_t parsed{0u};
                const std::from_chars_result result{std::from_chars(text, text_end, parsed)};
                if ((result.ec != std::errc{}) || (result.ptr != text_end)) {
                    throw std::runtime_error{argument + " needs a whole number, not \"" + std::string{text} + "\"."};
                }

                return parsed;
            };

            if (argument == "--record") {
                recording = true;
            } else if (argument == "--width") {
                record_width = value(record_width);
            } else if (argument == "--height") {
                record_height = value(record_height);
            } else if (argument == "--frames") {
                record_frames = value(record_frames);
            } else if ((argument == "--output") && ((index + 1) < argc)) {
                record_directory = argv[++index];
            }
        }

        const WindowLib::WindowConfig window_config{.title = "TronGrid Lite - debug window", .width = 1280, .height = 720, .resizable = true, .decorated = true};
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

        // The Grid: a flat mirror floor with neon tubes along its grid lines.
        const GridFloorConfig floor_config{.cells = 64u, .cell_size = 2.0f, .height = 0.0f};

        /*
            Thin tubes, sitting almost on the floor.

            The rasteriser of Phase 1 needed them wide and lifted, because a strip narrower than a
            pixel breaks into dashes and a coplanar strip fights the depth buffer. The tracer has
            neither problem: it samples geometry analytically and has no depth buffer at all, so
            the tubes can be the slender lines the aesthetic actually wants.
        */
        // The lift clears the steepest terrace gradient across the tube's half width; at 0.01 m
        // the outer edge of a strip dipped below the floor on riser cells.
        const NeonTubeConfig tube_config{.half_width = 0.025f, .surface_offset = 0.02f};

        const Mesh floor{generateGridFloor(floor_config)};
        const NeonGrid neon{generateGridFloorNeon(floor_config, tube_config)};

        const Mesh pillars{makePillars(floor_config)};
        const Mesh glass{makeGlassSlabs(floor_config)};
        const Mesh glowing_column{makeGlowingColumn(floor_config)};

        std::vector<BvhLib::Triangle> world_triangles;
        world_triangles.reserve(floor.triangleCount() + neon.primary.triangleCount() + neon.accent.triangleCount() + pillars.triangleCount() + glass.triangleCount()
            + glowing_column.triangleCount());
        appendTriangles(world_triangles, floor, MATERIAL_FLOOR);
        appendTriangles(world_triangles, neon.primary, MATERIAL_NEON_PRIMARY);
        appendTriangles(world_triangles, neon.accent, MATERIAL_NEON_ACCENT);
        appendTriangles(world_triangles, pillars, MATERIAL_PILLAR);
        appendTriangles(world_triangles, glass, MATERIAL_GLASS);
        appendTriangles(world_triangles, glowing_column, MATERIAL_GLOWING_GLASS);

        const std::chrono::steady_clock::time_point build_start{std::chrono::steady_clock::now()};
        const BvhLib::Bvh bvh{BvhLib::build(std::move(world_triangles))};
        const float build_milliseconds{std::chrono::duration<float, std::milli>{std::chrono::steady_clock::now() - build_start}.count()};

        logger.logInfo("Hierarchy built in " + std::to_string(static_cast<double>(build_milliseconds)) + " ms: " + std::to_string(bvh.triangles.size()) + " triangles, "
            + std::to_string(bvh.nodes.size()) + " nodes, depth " + std::to_string(bvh.depth()) + " of " + std::to_string(BvhLib::MAX_DEPTH) + ".");

        // Absolute paths, so the renderer runs from any working directory rather than only from
        // its own output folder.
        const std::filesystem::path shader_directory{executableDirectory()};

        const World world{device, bvh, logger};

        Tracer tracer{device, world, makeMaterials(), MAX_FRAMES_IN_FLIGHT, (shader_directory / "trace.spv").string(), logger};
        tracer.resize(swapchain.extent());

        PostProcess post_process{device, MAX_FRAMES_IN_FLIGHT, (shader_directory / "bloom.spv").string(), (shader_directory / "postprocess.spv").string(), logger};
        post_process.resize(swapchain.extent(), tracer.outputViews());

        if (recording) {
            return recordCinematic(device, tracer, post_process, logger, record_width, record_height, record_frames, record_directory);
        }

        /*
            From here the renderer runs on its own thread.

            Not for throughput — the GPU is the bottleneck and a thread does not make it faster. It
            is for responsiveness: on Win32 a modal resize drag runs the platform's own message loop,
            so a renderer sharing that thread simply stops until the User lets go of the window edge.
            The window procedure keeps firing throughout, which is why the event callback below can
            keep feeding the queue while the pump is stuck.

            The division of labour follows the constraint rather than taste. The message pump belongs
            to the thread that created the window, and `ShowCursor` is per-thread on Win32, so events
            and the cursor stay here. Everything Vulkan moves across, because a queue submission
            wants one owner.
        */
        RenderChannel channel;

        std::thread render_thread{[&device, &swapchain, &tracer, &post_process, &logger, &channel, &window]() {
            try {
                runRenderLoop(device, swapchain, tracer, post_process, logger, channel);
            } catch (const std::exception& error) {
                channel.render_failed = true;

                /*
                    Setting the flag is not enough on its own. The event thread spends nearly all of
                    its life asleep inside the platform's event wait, and nothing in this process can
                    end that sleep except an event — so without the wake, a dead renderer would sit
                    behind a window that still looked alive until the User happened to move the
                    mouse over it.

                    The condition variable is deliberately not signalled here: the only thread that
                    ever waits on it is this one.
                */
                try {
                    window->wakeEvents();
                }
                // NOLINTNEXTLINE(bugprone-empty-catch) — the flag above is the report.
                catch (...) {
                    // A window that cannot be woken is already gone; the flag still stops the loop.
                }

                try {
                    logger.logFatal(std::string{"Render thread: "} + error.what());
                }
                // NOLINTNEXTLINE(bugprone-empty-catch) — the flag above is the report.
                catch (...) {
                    // Nothing left that could report anything.
                }
            }
        }};

        /*
            Stops and joins the render thread, however this scope is left.

            A joinable std::thread that reaches its destructor calls std::terminate, and the platform
            calls in the loop below can throw — so without this, an error on the event thread would
            abort the process instead of being reported by the handler at the bottom of main. The
            ordinary exit runs through here too: a second stop-and-join written after the loop would
            only be a second thing to keep correct.

            Detaching the callback comes first and is not incidental. `window` is declared far above
            and is therefore destroyed long after `channel`; a close arriving in between would call
            through a pointer to an object that no longer exists.
        */
        struct RenderThreadGuard {
            WindowLib::Window* window;
            std::thread* thread;
            RenderChannel* channel;

            ~RenderThreadGuard()
            {
                window->setEventCallback(nullptr, nullptr);

                if (thread->joinable()) {
                    channel->send(RenderEvent{.type = RenderEvent::Type::Stop});
                    thread->join();
                }
            }
        } render_thread_guard{window.get(), &render_thread, &channel};

        /*
            The event thread from here on: pump, translate, forward.

            The callback fires from inside the platform's own message handling, which is what keeps
            the queue moving during a modal drag. It does no work beyond translation — anything slow
            here would stall the window.
        */
        window->setEventCallback(
            [](const WindowLib::WindowEvent& event, void* user_data) {
                auto* target = static_cast<RenderChannel*>(user_data);

                RenderEvent forwarded{};
                if (event.type == WindowLib::WindowEvent::Type::Resize) {
                    forwarded.type = RenderEvent::Type::Resize;
                    forwarded.width = event.resize.width;
                    forwarded.height = event.resize.height;
                } else {
                    forwarded.type = RenderEvent::Type::Input;
                    forwarded.input = event;
                }

                target->send(forwarded);
            },
            &channel);

        while (!window->shouldClose() && !channel.render_failed) {
            window->waitEvents();
            window->pumpEvents();

            // The callback has already forwarded everything; this drains the window's own queue so
            // it cannot grow without bound, and catches the close request.
            WindowLib::WindowEvent event{};
            while (window->pollEvent(event)) {
                if (event.type == WindowLib::WindowEvent::Type::Close) {
                    window->requestClose();
                }
            }

            // Applied here because ShowCursor is per-thread on Win32 and this is the window's thread.
            window->setCursorCaptured(channel.wants_cursor_capture);
        }

        logger.logInfo("Shutting down cleanly.");
    } catch (const std::exception& error) {
        /*
            The report is itself allowed to fail. Building the message allocates, and the likeliest
            reason to be down here in the first place is that allocation has stopped working — so an
            exception escaping `main` would replace a described failure with a bare terminate.
        */
        try {
            logger.logFatal(std::string{"Fatal error: "} + error.what());
        }
        // NOLINTNEXTLINE(bugprone-empty-catch) — the exit code below is the whole report now.
        catch (...) {
            // Deliberately swallowed: there is nothing left that could report anything.
        }

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
