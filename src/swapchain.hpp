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
#include <vector>

class Device; // forward declaration

/*!
    Owns the Vulkan swapchain of the User's window and its per-image views.
    Supports recreation on window resize.
*/
class Swapchain {
public:
    //! Create a swapchain for the given device and surface.
    Swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height, LoggingLib::Logger& logger);

    // Non-copyable, movable
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = default;
    Swapchain& operator=(Swapchain&&) = default;

    //! Recreate the swapchain (e.g., after window resize).
    void recreate(uint32_t width, uint32_t height);

    //! RAII swapchain reference.
    [[nodiscard]] const vk::raii::SwapchainKHR& get() const
    {
        return m_swapchain;
    }

    //! Swapchain images (non-owning, managed by the swapchain).
    [[nodiscard]] const std::vector<vk::Image>& images() const
    {
        return m_images;
    }

    //! Current swapchain extent.
    [[nodiscard]] vk::Extent2D extent() const
    {
        return m_extent;
    }

    //! Number of swapchain images.
    [[nodiscard]] uint32_t imageCount() const
    {
        return static_cast<uint32_t>(m_images.size());
    }

private:
    /*!
        Settles what this surface can do, once, and refuses it here if it cannot do enough.

        Format, present mode and image usage are properties of a surface-and-device pair rather than
        of a window size, so they are resolved at construction and never revisited. Two consequences
        follow, and both are the reason this is separate from `build`.

        **A surface the renderer cannot use is refused at startup rather than at the first resize.**
        Both conditions checked here — that any format exists at all, and that the surface accepts a
        transfer destination — are as true on the first frame as on the thousandth, so discovering
        them during a window drag would mean a failure whose timing had nothing to do with its cause.

        **And the format cannot change under the renderer.** Resolving it per rebuild would let a
        driver that reports its formats in a different order hand back a different swapchain format
        after a resize, while every pass downstream carried on assuming the first one.
    */
    void resolveSurfaceSettings();

    //! Internal: build the swapchain and its views at a size. Cannot fail on surface capability.
    void build(uint32_t width, uint32_t height);

    LoggingLib::Logger* m_logger{nullptr}; //!< Logger pointer (non-owning).
    const Device* m_device{nullptr}; //!< Back-pointer to the device (non-owning).
    VkSurfaceKHR m_surface{VK_NULL_HANDLE}; //!< Surface handle (non-owning).
    vk::raii::SwapchainKHR m_swapchain{nullptr}; //!< Swapchain handle.
    std::vector<vk::Image> m_images; //!< Swapchain images (non-owning).
    vk::SurfaceFormatKHR m_format{}; //!< Chosen surface format.
    vk::PresentModeKHR m_present_mode{}; //!< Chosen present mode.
    vk::Extent2D m_extent{}; //!< Current extent.
    vk::ImageUsageFlags m_image_usage{}; //!< Image usage flags actually requested.
};
