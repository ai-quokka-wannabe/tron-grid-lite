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

#include "device.hpp"
#include "memory_arena.hpp"
#include <vulkan/vulkan_raii.hpp>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

/*!
    The Vulkan chores every pass needs.

    One home for them rather than one per pass. Without it the first two land byte-for-byte in the
    tracer and in the post-processing stage, and a third time in `main.cpp` under a different name
    and with a different error message — and three copies of a function is three places to fix a bug
    in.

    Everything here needs a device or a physical device. Anything that does not belongs in a header
    that does not include Vulkan, so that a test of it needs no SDK and no GPU — see `spirv.hpp`.
*/
namespace VulkanHelpers
{

    /*!
        A device-local buffer.

        There is no memory member: the backing store belongs to the `MemoryArena` the buffer was
        uploaded into, which outlives it and reclaims everything at once.
    */
    struct DeviceBuffer {
        vk::raii::Buffer buffer{nullptr};
    };

    /*!
        Returns the index of a memory type satisfying both the resource's requirements and the
        requested properties.

        \param physical_device Device whose memory heaps are searched.
        \param type_bits Bitmask from the resource's `vk::MemoryRequirements`.
        \param required Properties the memory must have, such as host-visible and host-coherent.
        \return Index into the device's memory type array.
        \throws std::runtime_error when no memory type qualifies.
    */
    [[nodiscard]] inline uint32_t findMemoryType(const vk::raii::PhysicalDevice& physical_device, uint32_t type_bits, vk::MemoryPropertyFlags required)
    {
        const vk::PhysicalDeviceMemoryProperties properties{physical_device.getMemoryProperties()};

        for (uint32_t index{0u}; index < properties.memoryTypeCount; ++index) {
            if (((type_bits & (1u << index)) != 0u) && ((properties.memoryTypes[index].propertyFlags & required) == required)) {
                return index;
            }
        }

        throw std::runtime_error{"No memory type satisfies the requested properties."};
    }

    /*!
        Uploads bytes into a fresh device-local storage buffer through a staging copy.

        Device-local rather than host-visible, deliberately: these buffers are read many times per
        ray, and host-visible memory is the wrong side of the bus for that. The staging buffer and
        its command pool are local, so both are destroyed as soon as the copy has been waited on.

        **The staging buffer keeps its own plain allocation**, and the validation layer duly grumbles
        that it should have been sub-allocated. It is wrong here: the buffer exists for the duration
        of one copy and is gone before this function returns, so putting it in an arena would either
        grow that arena on every upload or need a reset protocol for a transfer nobody keeps.

        Synchronous — it submits and waits. Every caller runs at start-up, where one fence wait per
        buffer costs nothing and getting the data there before anybody draws is the whole point.

        \param device Logical device, its physical device and its graphics queue.
        \param arena Device-local arena the finished buffer is bound into. Must outlive the buffer.
        \param data Bytes to upload. Must be at least `bytes` long.
        \param bytes Size of the buffer. Must not be zero — Vulkan rejects a zero-sized buffer, so a
               caller with nothing to upload has to pass one placeholder element instead.
        \return The buffer and the memory backing it, both owned by the caller.
    */
    [[nodiscard]] inline DeviceBuffer uploadStorageBuffer(const Device& device, MemoryArena& arena, const void* data, vk::DeviceSize bytes)
    {
        const vk::raii::Buffer staging_buffer{
            device.get(), vk::BufferCreateInfo{.size = bytes, .usage = vk::BufferUsageFlagBits::eTransferSrc, .sharingMode = vk::SharingMode::eExclusive}};

        const vk::MemoryRequirements staging_requirements{staging_buffer.getMemoryRequirements()};
        const vk::raii::DeviceMemory staging_memory{device.get(),
            vk::MemoryAllocateInfo{.allocationSize = staging_requirements.size,
                .memoryTypeIndex = findMemoryType(device.physicalDevice(), staging_requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)}};
        staging_buffer.bindMemory(*staging_memory, 0u);

        void* mapped{staging_memory.mapMemory(0u, bytes)};
        std::memcpy(mapped, data, static_cast<size_t>(bytes));
        staging_memory.unmapMemory();

        DeviceBuffer result{};
        result.buffer = vk::raii::Buffer{device.get(),
            vk::BufferCreateInfo{
                .size = bytes, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .sharingMode = vk::SharingMode::eExclusive}};

        arena.bind(result.buffer);

        const vk::raii::CommandPool pool{
            device.get(), vk::CommandPoolCreateInfo{.flags = vk::CommandPoolCreateFlagBits::eTransient, .queueFamilyIndex = device.graphicsFamilyIndex()}};

        vk::raii::CommandBuffers command_buffers{
            device.get(), vk::CommandBufferAllocateInfo{.commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1u}};
        const vk::raii::CommandBuffer& command_buffer{command_buffers.front()};

        command_buffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        command_buffer.copyBuffer(*staging_buffer, *result.buffer, {vk::BufferCopy{.srcOffset = 0u, .dstOffset = 0u, .size = bytes}});
        command_buffer.end();

        const vk::raii::Fence fence{device.get(), vk::FenceCreateInfo{}};
        device.graphicsQueue().submit({vk::SubmitInfo{.commandBufferCount = 1u, .pCommandBuffers = &*command_buffer}}, *fence);

        while (device.get().waitForFences({*fence}, vk::True, UINT64_MAX) == vk::Result::eTimeout) {
            // Retry — a timeout here is not an error.
        }

        return result;
    }

} // namespace VulkanHelpers
