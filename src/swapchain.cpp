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

#include "swapchain.hpp"
#include "device.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <ranges>
#include <string>

namespace
{
    /*!
        Selects the surface format, preferring B8G8R8A8 UNORM with an sRGB-nonlinear colour space.

        UNORM rather than the sRGB variant deliberately: the post-processing pass applies the sRGB
        transfer function itself, and the display still interprets the values correctly because the
        colour space stays eSrgbNonlinear.
    */
    [[nodiscard]] vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& available)
    {
        assert(!available.empty());

        const std::vector<vk::SurfaceFormatKHR>::const_iterator preferred{std::ranges::find_if(available, [](const vk::SurfaceFormatKHR& fmt) {
            return (fmt.format == vk::Format::eB8G8R8A8Unorm) && (fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);
        })};

        return (preferred != available.end()) ? *preferred : available.front();
    }

    //! Selects the best present mode, preferring MAILBOX for low latency.
    [[nodiscard]] vk::PresentModeKHR choosePresentMode(const std::vector<vk::PresentModeKHR>& available)
    {
        // Prefer MAILBOX (low latency, no tearing).
        if (std::ranges::any_of(available, [](vk::PresentModeKHR mode) {
                return mode == vk::PresentModeKHR::eMailbox;
            })) {
            return vk::PresentModeKHR::eMailbox;
        }

        // FIFO is always available (guaranteed by spec).
        return vk::PresentModeKHR::eFifo;
    }

    //! Clamps the requested dimensions to the surface capability range.
    [[nodiscard]] vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height)
    {
        // If currentExtent is not the special 0xFFFFFFFF value, the surface size is fixed.
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }

        // Otherwise, clamp the window dimensions to the surface's allowed range.
        vk::Extent2D extent{};
        extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return extent;
    }
}

Swapchain::Swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height, LoggingLib::Logger& logger) :
    m_logger(&logger),
    m_device(&device),
    m_surface(surface)
{
    build(width, height);
}

void Swapchain::recreate(uint32_t width, uint32_t height)
{
    // Wait for all GPU work (including present operations) to complete before
    // destroying the old swapchain and its semaphores. Fences alone are not
    // sufficient — they only track command buffer completion, not present.
    m_device->get().waitIdle();

    // Clear old views first (they reference old images).
    m_images.clear();

    // Rebuild with old swapchain for smoother transition.
    build(width, height);
}

void Swapchain::build(uint32_t width, uint32_t height)
{
    // Guard against zero-extent (minimised window) — Vulkan requires imageExtent > 0.
    // Set m_extent to zero so callers that check swapchain.extent() skip rendering;
    // keeping a stale non-zero m_extent with empty m_images and m_views would silently
    // break acquireNextImage on the next iteration.
    if ((width == 0) || (height == 0)) {
        m_extent = vk::Extent2D{0, 0};
        return;
    }

    const vk::raii::PhysicalDevice& physical_device{m_device->physicalDevice()};

    // Query surface capabilities.
    const vk::SurfaceCapabilitiesKHR capabilities{physical_device.getSurfaceCapabilitiesKHR(m_surface)};
    const std::vector<vk::SurfaceFormatKHR> formats{physical_device.getSurfaceFormatsKHR(m_surface)};
    const std::vector<vk::PresentModeKHR> present_modes{physical_device.getSurfacePresentModesKHR(m_surface)};

    if (formats.empty()) {
        m_logger->logFatal("No surface formats available.");
        std::abort();
    }

    // Choose optimal settings.
    m_format = chooseSurfaceFormat(formats);
    m_present_mode = choosePresentMode(present_modes);
    m_extent = chooseExtent(capabilities, width, height);

    /*
        The guard above tested the *requested* size, but on Win32 chooseExtent always returns the
        surface's own currentExtent, and the window's cached size is only refreshed when a WM_SIZE
        is dequeued. Minimise the window between those two moments and the cached size is still
        non-zero while the surface already reports nothing at all — which would build a swapchain
        with a zero extent, violating VUID-VkSwapchainCreateInfoKHR-imageExtent-01689. Under NDEBUG
        no validation layer is there to say so, and the throw that follows carries no useful
        message. Re-checking what was actually resolved preserves the early-return contract above.
    */
    if ((m_extent.width == 0u) || (m_extent.height == 0u)) {
        m_extent = vk::Extent2D{0, 0};
        return;
    }

    // Image count: min + 1 for triple buffering headroom.
    uint32_t image_count{capabilities.minImageCount + 1};
    if ((capabilities.maxImageCount > 0) && (image_count > capabilities.maxImageCount)) {
        image_count = capabilities.maxImageCount;
    }

    /*
        Only COLOR_ATTACHMENT is guaranteed to appear in supportedUsageFlags, and
        transfer-destination is the one usage this renderer cannot do without: every frame ends in a
        blit into the swapchain image. On a surface that omits it, swapchain creation fails
        VUID-VkSwapchainCreateInfoKHR-imageUsage-01276 and surfaces as a generic fatal error with no
        indication of the cause.
    */
    if (!(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst)) {
        m_logger->logFatal("Surface does not support transfer-destination usage; the renderer cannot present its blit.");
        std::abort();
    }

    m_image_usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;

    // Create swapchain.
    vk::SwapchainCreateInfoKHR create_info{};
    create_info.surface = m_surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = m_format.format;
    create_info.imageColorSpace = m_format.colorSpace;
    create_info.imageExtent = m_extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = m_image_usage;

    const uint32_t graphics_family{m_device->graphicsFamilyIndex()};
    const uint32_t present_family{m_device->presentFamilyIndex()};
    const std::array<uint32_t, 2> family_indices{graphics_family, present_family};

    if (graphics_family != present_family) {
        create_info.imageSharingMode = vk::SharingMode::eConcurrent;
        create_info.setQueueFamilyIndices(family_indices);
    } else {
        create_info.imageSharingMode = vk::SharingMode::eExclusive;
    }

    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    create_info.presentMode = m_present_mode;
    create_info.clipped = vk::True;

    // Pass old swapchain for smoother recreation.
    create_info.oldSwapchain = *m_swapchain;

    m_swapchain = vk::raii::SwapchainKHR(m_device->get(), create_info);

    // Retrieve swapchain images.
    m_images = m_swapchain.getImages();

    m_logger->logInfo("Swapchain created: " + std::to_string(m_extent.width) + "x" + std::to_string(m_extent.height) + " (" + std::to_string(m_images.size())
        + " images, " + ((m_present_mode == vk::PresentModeKHR::eMailbox) ? "MAILBOX" : "FIFO") + ").");
}
