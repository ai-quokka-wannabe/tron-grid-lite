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

#include "device.hpp"
#include "instance.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

/*!
    Device extensions required in order to present.

    **The swapchain extension is required only when there is a surface**, which is why this is not one
    unconditional list. A headless run never creates a swapchain, so demanding the extension would
    refuse a compute-only card for lacking an ability it was never going to use.

    Dynamic rendering and synchronisation2 are Vulkan 1.3 core features and are requested through
    vk::PhysicalDeviceVulkan13Features rather than as extensions. Ray traversal runs in ordinary
    compute shaders, so none of the hardware ray-tracing extensions are requested.
*/
static constexpr std::array PRESENTATION_DEVICE_EXTENSIONS{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

//! Holds graphics and present queue family indices discovered during device selection.
struct QueueFamilyIndices {
    uint32_t graphics{UINT32_MAX}; //!< Graphics queue family index.
    uint32_t present{UINT32_MAX}; //!< Present queue family index; stays unset for a headless device.
    bool graphics_has_compute{false}; //!< Whether the chosen graphics family also supports compute.

    /*!
        Returns true if every queue family this device needs has been found.

        \param needs_present False for a headless device, which never presents and therefore needs no
               present family. Passing true when there is no surface would ask a device to prove it can
               present to nothing.
    */
    [[nodiscard]] bool isComplete(bool needs_present) const
    {
        return (graphics != UINT32_MAX) && ((present != UINT32_MAX) || !needs_present);
    }
};

//! Finds graphics and present queue family indices. A null surface skips the present search entirely.
[[nodiscard]] static QueueFamilyIndices findQueueFamilies(const vk::raii::PhysicalDevice& device, VkSurfaceKHR surface)
{
    QueueFamilyIndices indices;
    std::vector<vk::QueueFamilyProperties> families{device.getQueueFamilyProperties()};

    for (uint32_t i{0}; i < static_cast<uint32_t>(families.size()); ++i) {
        // Graphics support.
        if (families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            const bool has_compute{static_cast<bool>(families[i].queueFlags & vk::QueueFlagBits::eCompute)};

            /*
                A later graphics family never displaces an earlier one that also does compute.
                Overwriting unconditionally would let a device whose first family is
                graphics-plus-compute and whose second is graphics-without-compute end up selecting
                the one that cannot dispatch — and this renderer is nothing but compute dispatches.
            */
            if ((indices.graphics == UINT32_MAX) || has_compute || !indices.graphics_has_compute) {
                indices.graphics = i;
                indices.graphics_has_compute = has_compute;
            }
        }

        // Present support. Asking a driver whether it can present to a null surface is not a question
        // with an answer, so a headless search does not ask it.
        if (surface != VK_NULL_HANDLE) {
            vk::Bool32 present_support{device.getSurfaceSupportKHR(i, surface)};
            if (present_support) {
                indices.present = i;
            }

            // Prefer a single family that supports both (better performance).
            if ((indices.graphics == indices.present) && indices.isComplete(true)) {
                break;
            }
        }
    }

    return indices;
}

//! Checks whether the device supports the extensions required for the way it is about to be used.
[[nodiscard]] static bool hasRequiredExtensions(const vk::raii::PhysicalDevice& device, bool needs_presentation)
{
    if (!needs_presentation) {
        return true; // Nothing outside Vulkan 1.3 core is needed to compute.
    }

    std::vector<vk::ExtensionProperties> available{device.enumerateDeviceExtensionProperties()};

    for (const char* required : PRESENTATION_DEVICE_EXTENSIONS) {
        bool found{std::ranges::any_of(available, [required](const vk::ExtensionProperties& ext) {
            return std::string_view(ext.extensionName.data()) == required;
        })};
        if (!found) {
            return false;
        }
    }

    return true;
}

//! Checks whether the device exposes the Vulkan 1.3 core features the renderer relies on.
[[nodiscard]] static bool hasRequiredVulkan13Features(const vk::raii::PhysicalDevice& device)
{
    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features> features_chain{
        device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>()};
    const vk::PhysicalDeviceVulkan13Features& vulkan13{features_chain.get<vk::PhysicalDeviceVulkan13Features>()};

    return (vulkan13.dynamicRendering == vk::True) && (vulkan13.synchronization2 == vk::True);
}

//! Scores a physical device for suitability; returns -1 if unsuitable.
[[nodiscard]] static int rateDevice(const vk::raii::PhysicalDevice& device, VkSurfaceKHR surface)
{
    const bool needs_presentation{surface != VK_NULL_HANDLE};

    vk::PhysicalDeviceProperties properties{device.getProperties()};
    QueueFamilyIndices indices{findQueueFamilies(device, surface)};

    // Must support Vulkan 1.3 (for dynamic rendering and synchronisation2).
    if (properties.apiVersion < VK_API_VERSION_1_3) {
        return -1;
    }

    // Must have the queue families this use needs, and the swapchain extension if it will present.
    if ((!indices.isComplete(needs_presentation)) || (!hasRequiredExtensions(device, needs_presentation))) {
        return -1;
    }

    // Must expose the Vulkan 1.3 core features the renderer enables.
    if (!hasRequiredVulkan13Features(device)) {
        return -1;
    }

    /*
        Must be able to dispatch compute on the graphics family, because this renderer is nothing
        but compute dispatches.

        A rejection rather than a ten-point bonus, and the difference is not cosmetic. Scored as a
        bonus, a discrete GPU whose graphics family lacks compute still takes 10,000 for being
        discrete and beats a perfectly capable integrated device on 1,110 — so it gets selected, and
        the constructor below then aborts with the fatal message about compute while a GPU that would
        have worked sits unused. Rejecting here means the scoring loop simply passes over it.
    */
    if (!indices.graphics_has_compute) {
        return -1;
    }

    int score{0};

    // Strongly prefer discrete GPUs.
    if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
        score += 10000;
    } else if (properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) {
        score += 1000;
    }

    // Bonus for a single queue family (graphics + present on the same family). Headless there is no
    // present family, so no device earns it — which keeps the ordering between devices unchanged.
    if (needs_presentation && (indices.graphics == indices.present)) {
        score += 100;
    }

    return score;
}

//! Returns a human-readable name for a physical device type.
[[nodiscard]] static const char* deviceTypeName(vk::PhysicalDeviceType type)
{
    switch (type) {
    case vk::PhysicalDeviceType::eDiscreteGpu:
        return "discrete";
    case vk::PhysicalDeviceType::eIntegratedGpu:
        return "integrated";
    case vk::PhysicalDeviceType::eVirtualGpu:
        return "virtual";
    case vk::PhysicalDeviceType::eCpu:
        return "CPU";
    default:
        return "other";
    }
}

//! Formats a packed Vulkan version number as "major.minor.patch".
[[nodiscard]] static std::string formatVersion(uint32_t version)
{
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." + std::to_string(VK_API_VERSION_MINOR(version)) + "." + std::to_string(VK_API_VERSION_PATCH(version));
}

//! Formats a 32-bit value as a lower-case hexadecimal string with an "0x" prefix.
[[nodiscard]] static std::string formatHex(uint32_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << value;
    return stream.str();
}

//! Returns why a device cannot run this renderer, or an empty string if it can.
[[nodiscard]] static std::string rejectionReason(const vk::raii::PhysicalDevice& device, VkSurfaceKHR surface)
{
    const vk::PhysicalDeviceProperties properties{device.getProperties()};
    if (properties.apiVersion < VK_API_VERSION_1_3) {
        return "reports Vulkan " + formatVersion(properties.apiVersion) + ", needs 1.3";
    }

    const bool needs_presentation{surface != VK_NULL_HANDLE};

    const QueueFamilyIndices indices{findQueueFamilies(device, surface)};
    if (indices.graphics == UINT32_MAX) {
        return "no graphics queue family";
    }
    if (needs_presentation && (indices.present == UINT32_MAX)) {
        return "cannot present to this surface";
    }
    if (!indices.graphics_has_compute) {
        return "graphics queue family cannot dispatch compute, and this renderer is only compute";
    }
    if (!hasRequiredExtensions(device, needs_presentation)) {
        return "missing VK_KHR_swapchain";
    }
    if (!hasRequiredVulkan13Features(device)) {
        return "missing dynamic rendering or synchronisation2";
    }

    return {};
}

uint32_t Device::survey(const Instance& instance, VkSurfaceKHR surface, LoggingLib::Logger& logger)
{
    const std::vector<vk::raii::PhysicalDevice> physical_devices{instance.get().enumeratePhysicalDevices()};

    logger.logInfo("Vulkan devices on this machine: " + std::to_string(physical_devices.size()) + ".");

    uint32_t usable{0u};
    for (size_t index{0u}; index < physical_devices.size(); ++index) {
        const vk::PhysicalDeviceProperties properties{physical_devices[index].getProperties()};
        const std::string reason{rejectionReason(physical_devices[index], surface)};
        const int score{rateDevice(physical_devices[index], surface)};

        std::string line{"  [" + std::to_string(index) + "] " + properties.deviceName.data() + " — " + deviceTypeName(properties.deviceType) + ", Vulkan "
            + formatVersion(properties.apiVersion)};

        if (reason.empty()) {
            ++usable;
            logger.logInfo(line + " — USABLE, score " + std::to_string(score) + ". Run with --gpu " + std::to_string(index) + ".");
        } else {
            logger.logInfo(line + " — unusable: " + reason + ".");
        }
    }

    if (usable == 0u) {
        logger.logWarning("No device on this machine can run TronGrid Lite.");
    }

    return usable;
}

Device::Device(const Instance& instance, VkSurfaceKHR surface, LoggingLib::Logger& logger, uint32_t preferred_index) :
    m_logger(logger)
{
    // Step 1: Enumerate physical devices.
    std::vector<vk::raii::PhysicalDevice> physical_devices{instance.get().enumeratePhysicalDevices()};
    if (physical_devices.empty()) {
        m_logger.logFatal("No Vulkan-capable GPU found.");
        std::abort();
    }

    // Step 2: Score and pick the best device.
    int best_score{-1};
    size_t best_index{0};

    for (size_t i{0}; i < physical_devices.size(); ++i) {
        int score{rateDevice(physical_devices[i], surface)};
        vk::PhysicalDeviceProperties props{physical_devices[i].getProperties()};
        m_logger.logInfo("GPU " + std::to_string(i) + ": " + props.deviceName.data() + " (score: " + std::to_string(score) + ").");

        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }

    if (best_score < 0) {
        m_logger.logFatal(std::string{"No suitable GPU found (need Vulkan 1.3 with dynamic rendering and synchronisation2, and a graphics family that dispatches compute"}
            + ((surface != VK_NULL_HANDLE) ? ", plus a present queue and VK_KHR_swapchain)." : ")."));
        std::abort();
    }

    /*
        An explicit choice overrides the score, but never the suitability check: a device that cannot
        present, or lacks the Vulkan 1.3 features this renderer enables, is refused however loudly it
        was asked for. Refusing with a clear reason beats honouring the request and failing later
        inside a call that does not mention the GPU at all.
    */
    if (preferred_index != NO_PREFERENCE) {
        if (preferred_index >= physical_devices.size()) {
            m_logger.logFatal("Requested GPU " + std::to_string(preferred_index) + " but only " + std::to_string(physical_devices.size()) + " are present.");
            std::abort();
        }

        if (rateDevice(physical_devices[preferred_index], surface) < 0) {
            m_logger.logFatal("Requested GPU " + std::to_string(preferred_index) + " cannot run this renderer; see the scores above.");
            std::abort();
        }

        best_index = preferred_index;
        m_logger.logInfo("GPU " + std::to_string(preferred_index) + " chosen explicitly, overriding the score.");
    }

    m_physical_device = std::move(physical_devices[best_index]);
    vk::PhysicalDeviceProperties properties{m_physical_device.getProperties()};
    m_device_name = properties.deviceName.data();

    // The driver version is vendor-encoded rather than Vulkan-packed, so it is logged raw in
    // hexadecimal — decoding it as major.minor.patch would be wrong for most vendors.
    m_logger.logInfo("Selected GPU: " + m_device_name + " (Vulkan API " + formatVersion(properties.apiVersion) + ", driver version " + formatHex(properties.driverVersion)
        + ", vendor ID " + formatHex(properties.vendorID) + ").");

    // Step 3: Find queue families.
    QueueFamilyIndices indices{findQueueFamilies(m_physical_device, surface)};
    m_graphics_family_index = indices.graphics;
    m_present_family_index = indices.present;
    if (!indices.graphics_has_compute) {
        /*
            Fatal, not a warning. A warning here would announce that the traversal passes cannot be
            dispatched, and then execution would carry on to record a compute dispatch into a pool
            created on this very family, which is VUID-vkCmdDispatch-commandBuffer-cmdpool. The
            outcome is a garbled or hung GPU rather than a clean refusal, and the log line that
            predicted it scrolls away unread.
        */
        m_logger.logFatal("Graphics queue family does not support compute; this renderer is nothing but compute dispatches.");
        std::abort();
    }

    // Step 4: Create the logical device.
    constexpr float QUEUE_PRIORITY{1.0f};
    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;

    // Deduplicate queue family indices. Headless there is no present family, and UINT32_MAX must not
    // reach vkCreateDevice as one — the set would otherwise carry the sentinel straight through.
    std::set<uint32_t> unique_families{m_graphics_family_index};
    if (m_present_family_index != UINT32_MAX) {
        unique_families.insert(m_present_family_index);
    }
    for (uint32_t family : unique_families) {
        vk::DeviceQueueCreateInfo queue_info{};
        queue_info.queueFamilyIndex = family;
        queue_info.setQueuePriorities(QUEUE_PRIORITY);
        queue_create_infos.push_back(queue_info);
    }

    // Enable the Vulkan 1.3 core features: dynamic rendering (no VkRenderPass / VkFramebuffer)
    // and synchronisation2 (the barrier form used throughout the renderer).
    vk::PhysicalDeviceVulkan13Features vulkan13_features{};
    vulkan13_features.dynamicRendering = vk::True;
    vulkan13_features.synchronization2 = vk::True;

    vk::PhysicalDeviceFeatures2 features2{};
    features2.setPNext(&vulkan13_features);

    // Enabled only when this device will actually present. Enabling an extension that is not needed
    // is not free of consequence: on a device that does not expose it, vkCreateDevice fails outright.
    const std::vector<const char*> enabled_extensions{(surface != VK_NULL_HANDLE)
            ? std::vector<const char*>{PRESENTATION_DEVICE_EXTENSIONS.begin(), PRESENTATION_DEVICE_EXTENSIONS.end()}
            : std::vector<const char*>{}};

    vk::DeviceCreateInfo device_info{};
    device_info.setPNext(&features2);
    device_info.setQueueCreateInfos(queue_create_infos);
    device_info.setPEnabledExtensionNames(enabled_extensions);

    m_device = vk::raii::Device(m_physical_device, device_info);

    // Step 5: Load device-level function pointers.
    volkLoadDevice(*m_device);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_device);

    // Step 6: Retrieve queue handles. There is no present queue on a headless device, and asking for
    // family UINT32_MAX would be a use of an unretrieved handle rather than an empty one.
    m_graphics_queue = m_device.getQueue(m_graphics_family_index, 0);
    if (m_present_family_index != UINT32_MAX) {
        m_present_queue = m_device.getQueue(m_present_family_index, 0);
    }
}
