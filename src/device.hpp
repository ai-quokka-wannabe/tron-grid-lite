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
#include <cstdint>
#include <string>

class Instance; // forward declaration

/*!
    Selects the best physical device and creates a logical device.

    The requirements are deliberately modest: Vulkan 1.3 core (dynamic rendering and
    synchronisation2), and a queue family that can dispatch compute. Ray traversal is performed by
    hand in ordinary compute shaders against a self-built BVH held in storage buffers, so no hardware
    ray-tracing or mesh-shader extension is requested and the device is creatable on modest GPUs such
    as a GTX 1650 Ti.

    **Presentation is optional, and that is the point.** TronGrid Lite is a command-line program that
    can open a window, not a window that can be scripted: creatures perceive the Grid through a senses
    buffer and never through a swapchain, so a training run needs no display and must not be refused
    for lacking one. Passing a null surface asks for a device that can compute; passing a real one
    additionally asks that it can present.

    The difference is not cosmetic. Requiring presentation refuses a compute-only card with no monitor
    attached — exactly the hardware a headless run would otherwise be happiest on.
*/
class Device {
public:
    /*!
        Selects a physical device and creates a logical one.

        \param instance Vulkan instance to enumerate from.
        \param surface Surface the device must be able to present to, or `VK_NULL_HANDLE` for a
               headless device. With a null surface no present queue is sought, `VK_KHR_swapchain` is
               not required and not enabled, and `presentQueue()` must not be called.
        \param logger Logger for the selection summary.
        \param preferred_index Index of the GPU to use, as printed in the selection summary. Pass
               `NO_PREFERENCE` to take the highest-scoring device, which is what everything except
               deliberate cross-vendor testing wants.
    */
    Device(const Instance& instance, VkSurfaceKHR surface, LoggingLib::Logger& logger, uint32_t preferred_index = NO_PREFERENCE);

    /*!
        Take the highest-scoring device rather than a named one.

        Scoring strongly prefers a discrete GPU, so on a laptop with switchable graphics this is
        always the discrete one — which is exactly why the override exists. A bug that only appears
        on the integrated driver would otherwise never be seen, and this repository has already
        shipped one piece of reasoning whose only evidence was "it works on the driver in front of
        me".
    */
    static constexpr uint32_t NO_PREFERENCE{UINT32_MAX};

    /*!
        Logs every Vulkan device on this machine and whether it can run the renderer.

        Reports *why* an unusable device is unusable rather than only that it scored badly, because
        "no suitable GPU found" is the least actionable message a renderer can print. Creates no
        logical device and leaves nothing behind.

        \param instance Vulkan instance to enumerate from.
        \param surface Surface presentability is tested against.
        \param logger Logger to report through.
        \return How many devices can run the renderer.
    */
    [[nodiscard]] static uint32_t survey(const Instance& instance, VkSurfaceKHR surface, LoggingLib::Logger& logger);

    // Non-copyable, non-movable
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    //! RAII physical device reference.
    [[nodiscard]] const vk::raii::PhysicalDevice& physicalDevice() const
    {
        return m_physical_device;
    }

    //! RAII device reference.
    [[nodiscard]] const vk::raii::Device& get() const
    {
        return m_device;
    }

    //! Graphics queue handle.
    [[nodiscard]] const vk::raii::Queue& graphicsQueue() const
    {
        return m_graphics_queue;
    }

    //! Present queue handle.
    [[nodiscard]] const vk::raii::Queue& presentQueue() const
    {
        return m_present_queue;
    }

    //! Graphics queue family index.
    [[nodiscard]] uint32_t graphicsFamilyIndex() const
    {
        return m_graphics_family_index;
    }

    //! Present queue family index.
    [[nodiscard]] uint32_t presentFamilyIndex() const
    {
        return m_present_family_index;
    }

    //! Human-readable GPU name.
    [[nodiscard]] const std::string& name() const
    {
        return m_device_name;
    }

private:
    LoggingLib::Logger& m_logger; //!< Logger reference (non-owning).
    vk::raii::PhysicalDevice m_physical_device{nullptr}; //!< Selected physical device.
    vk::raii::Device m_device{nullptr}; //!< Logical device handle.
    vk::raii::Queue m_graphics_queue{nullptr}; //!< Graphics queue.
    vk::raii::Queue m_present_queue{nullptr}; //!< Present queue.
    uint32_t m_graphics_family_index{UINT32_MAX}; //!< Graphics queue family index (sentinel until assigned).
    uint32_t m_present_family_index{UINT32_MAX}; //!< Present queue family index (sentinel until assigned).
    std::string m_device_name; //!< Human-readable GPU name.
};
