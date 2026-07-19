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
    Phase 0 — prove the toolchain.

    Brings up the spectator window, a Vulkan instance, device and swapchain, then draws a single
    triangle with dynamic rendering. The triangle is a smoke test for the graphics queue: the
    world itself will be traced in compute shaders from Phase 2 onwards. There is no player and
    no input handling here — the window exists so a human can watch, nothing more.
*/

#include "device.hpp"
#include "instance.hpp"
#include "surface.hpp"
#include "swapchain.hpp"
#include <log/logger.hpp>
#include <math/matrix.hpp>
#include <math/projection.hpp>
#include <math/vector.hpp>
#include <window/window.hpp>
#include <window/window_event.hpp>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <vector>

namespace
{

    //! Number of frames the host may record ahead of the GPU.
    constexpr uint32_t MAX_FRAMES_IN_FLIGHT{2};

    //! Per-vertex data, matching the vertex input layout documented in triangle.slang.
    struct Vertex {
        float position[3]; //!< Object-space position.
        float normal[3]; //!< Object-space normal, visualised as colour.
        float uv[2]; //!< Texture coordinate, unused in Phase 0.
    };

    //! Reads a compiled SPIR-V module from disk, next to the executable.
    [[nodiscard]] std::vector<uint32_t> readSpirv(const std::string& path)
    {
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file.is_open()) {
            throw std::runtime_error{"Failed to open SPIR-V module: " + path};
        }

        const std::streamsize size_bytes{file.tellg()};
        if ((size_bytes <= 0) || ((size_bytes % 4) != 0)) {
            throw std::runtime_error{"SPIR-V module has an invalid size: " + path};
        }

        std::vector<uint32_t> words(static_cast<size_t>(size_bytes) / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(words.data()), size_bytes);
        return words;
    }

    //! Creates the Phase 0 graphics pipeline: a descriptor-free triangle drawn with dynamic rendering.
    [[nodiscard]] vk::raii::Pipeline createTrianglePipeline(const vk::raii::Device& device, const vk::raii::PipelineLayout& layout, vk::Format colour_format,
        const std::string& shader_path)
    {
        const std::vector<uint32_t> code{readSpirv(shader_path)};

        const vk::ShaderModuleCreateInfo module_info{.codeSize = code.size() * sizeof(uint32_t), .pCode = code.data()};
        const vk::raii::ShaderModule shader_module{device, module_info};

        const std::array<vk::PipelineShaderStageCreateInfo, 2> stages{
            vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = *shader_module, .pName = "vertMain"},
            vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = *shader_module, .pName = "fragMain"}};

        const vk::VertexInputBindingDescription binding{.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};

        const std::array<vk::VertexInputAttributeDescription, 3> attributes{
            vk::VertexInputAttributeDescription{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, position)},
            vk::VertexInputAttributeDescription{.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, normal)},
            vk::VertexInputAttributeDescription{.location = 2, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, uv)}};

        const vk::PipelineVertexInputStateCreateInfo vertex_input{.vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &binding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions = attributes.data()};

        const vk::PipelineInputAssemblyStateCreateInfo input_assembly{.topology = vk::PrimitiveTopology::eTriangleList, .primitiveRestartEnable = vk::False};

        const vk::PipelineViewportStateCreateInfo viewport_state{.viewportCount = 1, .scissorCount = 1};

        const vk::PipelineRasterizationStateCreateInfo rasterisation{.depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eNone,
            .frontFace = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable = vk::False,
            .lineWidth = 1.0f};

        const vk::PipelineMultisampleStateCreateInfo multisample{.rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False};

        const vk::PipelineColorBlendAttachmentState blend_attachment{.blendEnable = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

        const vk::PipelineColorBlendStateCreateInfo colour_blend{.logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blend_attachment};

        const std::array<vk::DynamicState, 2> dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{
            .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()), .pDynamicStates = dynamic_states.data()};

        // Dynamic rendering — no VkRenderPass, no VkFramebuffer.
        const vk::PipelineRenderingCreateInfo rendering_info{.colorAttachmentCount = 1, .pColorAttachmentFormats = &colour_format};

        const vk::GraphicsPipelineCreateInfo pipeline_info{.pNext = &rendering_info,
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterisation,
            .pMultisampleState = &multisample,
            .pColorBlendState = &colour_blend,
            .pDynamicState = &dynamic_state,
            .layout = *layout};

        return vk::raii::Pipeline{device, nullptr, pipeline_info};
    }

    //! Finds a device-local, host-visible memory type for the small Phase 0 vertex buffer.
    [[nodiscard]] uint32_t findMemoryType(const vk::raii::PhysicalDevice& physical_device, uint32_t type_bits, vk::MemoryPropertyFlags required)
    {
        const vk::PhysicalDeviceMemoryProperties properties{physical_device.getMemoryProperties()};
        for (uint32_t index{0}; index < properties.memoryTypeCount; ++index) {
            if (((type_bits & (1u << index)) != 0) && ((properties.memoryTypes[index].propertyFlags & required) == required)) {
                return index;
            }
        }
        throw std::runtime_error{"No suitable memory type for the vertex buffer."};
    }

} // namespace

int main()
{
    LoggingLib::Logger logger;

    try {
        logger.logInfo("TronGrid Lite — Phase 0 (a world for AI agents; this window is for the human observer only).");

        const WindowLib::WindowConfig window_config{.title = "TronGrid Lite — spectator", .width = 1280, .height = 720, .resizable = true, .decorated = true};
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

        // A single triangle standing upright in front of the camera. Its normals become its colours.
        constexpr std::array<Vertex, 3> vertices{Vertex{{0.0f, 0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}},
            Vertex{{-0.6f, -0.4f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}}, Vertex{{0.6f, -0.4f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}}};

        const vk::BufferCreateInfo buffer_info{.size = sizeof(vertices), .usage = vk::BufferUsageFlagBits::eVertexBuffer, .sharingMode = vk::SharingMode::eExclusive};
        const vk::raii::Buffer vertex_buffer{device.get(), buffer_info};

        const vk::MemoryRequirements requirements{vertex_buffer.getMemoryRequirements()};
        const vk::MemoryAllocateInfo allocate_info{.allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(device.physicalDevice(), requirements.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)};
        const vk::raii::DeviceMemory vertex_memory{device.get(), allocate_info};
        vertex_buffer.bindMemory(*vertex_memory, 0);

        void* mapped{vertex_memory.mapMemory(0, sizeof(vertices))};
        std::memcpy(mapped, vertices.data(), sizeof(vertices));
        vertex_memory.unmapMemory();

        const vk::PushConstantRange push_range{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(MathLib::Mat4)};
        const vk::PipelineLayoutCreateInfo layout_info{.setLayoutCount = 0, .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range};
        const vk::raii::PipelineLayout pipeline_layout{device.get(), layout_info};

        vk::raii::Pipeline pipeline{createTrianglePipeline(device.get(), pipeline_layout, swapchain.format().format, "triangle.spv")};

        const vk::CommandPoolCreateInfo pool_info{.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = device.graphicsFamilyIndex()};
        const vk::raii::CommandPool command_pool{device.get(), pool_info};

        const vk::CommandBufferAllocateInfo command_buffer_info{
            .commandPool = *command_pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = MAX_FRAMES_IN_FLIGHT};
        vk::raii::CommandBuffers command_buffers{device.get(), command_buffer_info};

        std::vector<vk::raii::Semaphore> image_available;
        std::vector<vk::raii::Fence> in_flight;
        for (uint32_t frame{0}; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
            image_available.emplace_back(device.get(), vk::SemaphoreCreateInfo{});
            in_flight.emplace_back(device.get(), vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
        }

        // One present semaphore per swapchain image: a semaphore signalled for image N must not be
        // waited on while a different frame presents image M.
        std::vector<vk::raii::Semaphore> render_finished;
        for (uint32_t image{0}; image < swapchain.imageCount(); ++image) {
            render_finished.emplace_back(device.get(), vk::SemaphoreCreateInfo{});
        }

        logger.logInfo("Phase 0 initialised on " + device.name() + " — drawing the smoke-test triangle.");

        uint32_t frame_index{0};
        bool needs_recreate{false};

        while (!window->shouldClose()) {
            window->pumpEvents();

            WindowLib::WindowEvent event{};
            while (window->pollEvent(event)) {
                if (event.type == WindowLib::WindowEvent::Type::Resize) {
                    needs_recreate = true;
                } else if (event.type == WindowLib::WindowEvent::Type::Close) {
                    window->requestClose();
                }
            }

            // A minimised window has a zero-sized swapchain; there is nothing to render into.
            if ((window->width() == 0) || (window->height() == 0)) {
                window->waitEvents();
                continue;
            }

            if (needs_recreate) {
                device.get().waitIdle();
                swapchain.recreate(window->width(), window->height());
                needs_recreate = false;
                continue;
            }

            const vk::raii::Fence& fence{in_flight[frame_index]};
            while (device.get().waitForFences({*fence}, vk::True, UINT64_MAX) == vk::Result::eTimeout) {
                // Retry — a timeout here is not an error.
            }

            uint32_t image_index{0};
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

            const vk::raii::CommandBuffer& command_buffer{command_buffers[frame_index]};
            command_buffer.reset();
            command_buffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

            const vk::ImageMemoryBarrier2 to_colour_attachment{.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                .srcAccessMask = vk::AccessFlagBits2::eNone,
                .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                .image = swapchain.images()[image_index],
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_colour_attachment});

            // Infinite black — the world's default background, and the reason emissive geometry reads as neon.
            const vk::ClearValue clear_colour{vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}};
            const vk::RenderingAttachmentInfo colour_attachment{.imageView = *swapchain.views()[image_index],
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .clearValue = clear_colour};

            const vk::RenderingInfo rendering{
                .renderArea = vk::Rect2D{{0, 0}, swapchain.extent()}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &colour_attachment};

            command_buffer.beginRendering(rendering);
            command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

            const vk::Viewport viewport{.x = 0.0f,
                .y = 0.0f,
                .width = static_cast<float>(swapchain.extent().width),
                .height = static_cast<float>(swapchain.extent().height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f};
            command_buffer.setViewport(0, {viewport});
            command_buffer.setScissor(0, {vk::Rect2D{{0, 0}, swapchain.extent()}});

            const float aspect{static_cast<float>(swapchain.extent().width) / static_cast<float>(swapchain.extent().height)};
            const MathLib::Mat4 projection{MathLib::perspective(MathLib::PI / 4.0f, aspect, 0.1f, 100.0f)};
            const MathLib::Mat4 view{MathLib::Mat4::translate(MathLib::Vec3{0.0f, 0.0f, -2.0f})};
            const MathLib::Mat4 model_view_projection{projection * view};
            command_buffer.pushConstants<MathLib::Mat4>(*pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                {model_view_projection});

            command_buffer.bindVertexBuffers(0, {*vertex_buffer}, {0});
            command_buffer.draw(static_cast<uint32_t>(vertices.size()), 1, 0, 0);
            command_buffer.endRendering();

            const vk::ImageMemoryBarrier2 to_present{.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe,
                .dstAccessMask = vk::AccessFlagBits2::eNone,
                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .newLayout = vk::ImageLayout::ePresentSrcKHR,
                .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                .image = swapchain.images()[image_index],
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_present});

            command_buffer.end();

            const vk::PipelineStageFlags wait_stage{vk::PipelineStageFlagBits::eColorAttachmentOutput};
            const vk::SubmitInfo submit{.waitSemaphoreCount = 1,
                .pWaitSemaphores = &*image_available[frame_index],
                .pWaitDstStageMask = &wait_stage,
                .commandBufferCount = 1,
                .pCommandBuffers = &*command_buffer,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &*render_finished[image_index]};
            device.graphicsQueue().submit({submit}, *fence);

            try {
                const vk::PresentInfoKHR present{.waitSemaphoreCount = 1,
                    .pWaitSemaphores = &*render_finished[image_index],
                    .swapchainCount = 1,
                    .pSwapchains = &*swapchain.get(),
                    .pImageIndices = &image_index};
                const vk::Result result{device.presentQueue().presentKHR(present)};
                if (result == vk::Result::eSuboptimalKHR) {
                    needs_recreate = true;
                }
            } catch (const vk::OutOfDateKHRError&) {
                needs_recreate = true;
            }

            frame_index = (frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
        }

        device.get().waitIdle();
        logger.logInfo("Shutting down cleanly.");
    } catch (const std::exception& error) {
        logger.logFatal(std::string{"Fatal error: "} + error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
