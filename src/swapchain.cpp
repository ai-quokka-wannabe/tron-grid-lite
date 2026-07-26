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
    //! True when the given format may be used as a storage image with optimal tiling.
    [[nodiscard]] bool formatSupportsStorageWrites(const vk::raii::PhysicalDevice& physical_device, vk::Format format)
    {
        const vk::FormatProperties format_props{physical_device.getFormatProperties(format)};
        return static_cast<bool>(format_props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage);
    }

    /*!
        Selects the best surface format, preferring B8G8R8A8 UNORM so that the compute ray
        tracer can write its result straight into the presented image. UNORM is chosen over
        the sRGB variant deliberately: sRGB formats cannot be used as storage images, and the
        tracer applies the sRGB transfer function itself. The display still interprets the
        values as sRGB because the colour space stays eSrgbNonlinear.
    */
    [[nodiscard]] vk::SurfaceFormatKHR chooseSurfaceFormat(const vk::raii::PhysicalDevice& physical_device, const std::vector<vk::SurfaceFormatKHR>& available)
    {
        assert(!available.empty());

        // First choice: the canonical BGRA UNORM / sRGB-nonlinear pair, if it can take storage writes.
        const std::vector<vk::SurfaceFormatKHR>::const_iterator preferred{std::ranges::find_if(available, [&physical_device](const vk::SurfaceFormatKHR& fmt) {
            return (fmt.format == vk::Format::eB8G8R8A8Unorm) && (fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                && formatSupportsStorageWrites(physical_device, fmt.format);
        })};

        if (preferred != available.end()) {
            return *preferred;
        }

        // Second choice: any offered format that supports storage writes.
        const std::vector<vk::SurfaceFormatKHR>::const_iterator storage_capable{std::ranges::find_if(available, [&physical_device](const vk::SurfaceFormatKHR& fmt) {
            return formatSupportsStorageWrites(physical_device, fmt.format);
        })};

        if (storage_capable != available.end()) {
            return *storage_capable;
        }

        // Last resort: whatever the surface offers first. The tracer will then have to render
        // offscreen and copy into the swapchain image via eTransferDst.
        return available.front();
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
    m_views.clear();
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
    m_format = chooseSurfaceFormat(physical_device, formats);
    m_present_mode = choosePresentMode(present_modes);
    m_extent = chooseExtent(capabilities, width, height);

    // Image count: min + 1 for triple buffering headroom.
    uint32_t image_count{capabilities.minImageCount + 1};
    if ((capabilities.maxImageCount > 0) && (image_count > capabilities.maxImageCount)) {
        image_count = capabilities.maxImageCount;
    }

    /*
        The compute ray tracer would like to write its result straight into the acquired
        swapchain image. That requires two independent things: the surface must advertise
        eStorage in its supported usage flags, and the chosen format must expose the storage
        image feature with optimal tiling. Neither is guaranteed, so both are probed and the
        absence of either is a logged degradation rather than a fatal error — the images are
        always created with eTransferDst, so the tracer can fall back to rendering into an
        offscreen image and copying it across.
    */
    const bool surface_allows_storage{static_cast<bool>(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eStorage)};
    const bool format_allows_storage{formatSupportsStorageWrites(physical_device, m_format.format)};
    m_storage_writes_supported = (surface_allows_storage && format_allows_storage);

    if (!surface_allows_storage) {
        m_logger->logWarning("Surface does not support storage image usage; the tracer must present via a transfer copy.");
    } else if (!format_allows_storage) {
        m_logger->logWarning("Chosen swapchain format does not support storage image writes; the tracer must present via a transfer copy.");
    }

    m_image_usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
    if (m_storage_writes_supported) {
        m_image_usage |= vk::ImageUsageFlagBits::eStorage;
    }

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
        + " images, " + ((m_present_mode == vk::PresentModeKHR::eMailbox) ? "MAILBOX" : "FIFO") + ", " + (m_storage_writes_supported ? "storage writes" : "transfer copy")
        + ").");

    // Create image views.
    m_views.clear();
    m_views.reserve(m_images.size());

    for (vk::Image image : m_images) {
        vk::ImageViewCreateInfo view_info{};
        view_info.image = image;
        view_info.viewType = vk::ImageViewType::e2D;
        view_info.format = m_format.format;
        view_info.components.r = vk::ComponentSwizzle::eIdentity;
        view_info.components.g = vk::ComponentSwizzle::eIdentity;
        view_info.components.b = vk::ComponentSwizzle::eIdentity;
        view_info.components.a = vk::ComponentSwizzle::eIdentity;
        view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        m_views.push_back(vk::raii::ImageView(m_device->get(), view_info));
    }
}
