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

#include "memory_arena.hpp"
#include "device.hpp"
#include "vulkan_helpers.hpp"
#include <algorithm>
#include <cstddef>

namespace
{

    //! Rounds `value` up to the next multiple of `alignment`, which must be a power of two.
    [[nodiscard]] vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
    {
        if (alignment <= 1u) {
            return value;
        }
        return ((value + alignment) - 1u) & ~(alignment - 1u);
    }

} // namespace

MemoryArena::MemoryArena(const Device& device, vk::MemoryPropertyFlags properties, vk::DeviceSize block_bytes) :
    m_device(&device),
    m_properties(properties),
    m_block_bytes(block_bytes)
{
    m_buffer_image_granularity = device.physicalDevice().getProperties().limits.bufferImageGranularity;
}

MemoryArena::Placement MemoryArena::place(const vk::MemoryRequirements& requirements, bool linear)
{
    const uint32_t memory_type{VulkanHelpers::findMemoryType(m_device->physicalDevice(), requirements.memoryTypeBits, m_properties)};

    for (Block& block : m_blocks) {
        if (block.memory_type_index != memory_type) {
            continue;
        }

        vk::DeviceSize offset{alignUp(block.used, requirements.alignment)};

        /*
            A buffer and an optimally-tiled image may not share a `bufferImageGranularity` page: the
            driver is entitled to treat that page as belonging entirely to one or the other, and
            overlapping them is undefined behaviour that no validation layer is obliged to catch.

            Padding to the granularity whenever the kind changes sidesteps the whole question and
            costs at most one page per transition — and in this renderer there are no transitions at
            all, because every arena is handed one kind of resource. The code is here so that stays
            true by construction rather than by everyone remembering.
        */
        if (block.occupied && (block.holds_linear != linear)) {
            offset = alignUp(offset, m_buffer_image_granularity);
        }

        if ((offset + requirements.size) <= block.capacity) {
            block.used = offset + requirements.size;
            block.holds_linear = linear;
            block.occupied = true;
            return Placement{.memory = *block.memory, .offset = offset, .mapped = (block.mapped != nullptr) ? (static_cast<std::byte*>(block.mapped) + offset) : nullptr};
        }
    }

    // Nothing had room, so take a fresh block — at least big enough for this resource, since a
    // resource larger than the preferred block size must still be servable.
    const vk::DeviceSize capacity{std::max(m_block_bytes, requirements.size)};

    Block block{};
    block.memory_type_index = memory_type;
    block.memory = vk::raii::DeviceMemory{m_device->get(), vk::MemoryAllocateInfo{.allocationSize = capacity, .memoryTypeIndex = memory_type}};
    block.capacity = capacity;
    block.used = requirements.size;
    block.holds_linear = linear;
    block.occupied = true;

    /*
        Mapped once, here, and never unmapped: freeing the memory unmaps it, and Vulkan explicitly
        permits a mapping to outlive every access to it. Mapping per buffer would be illegal rather
        than merely wasteful — one `VkDeviceMemory` may be mapped only once, and two buffers sharing
        a block would each try.
    */
    if ((m_properties & vk::MemoryPropertyFlagBits::eHostVisible) == vk::MemoryPropertyFlagBits::eHostVisible) {
        block.mapped = block.memory.mapMemory(0u, capacity);
    }

    const Placement placement{.memory = *block.memory, .offset = 0u, .mapped = block.mapped};
    m_blocks.push_back(std::move(block));
    return placement;
}

void MemoryArena::bind(const vk::raii::Image& image)
{
    // Every image in this renderer is optimally tiled, so none of them is linear. Passing the answer
    // rather than querying it keeps the granularity rule readable at the one place it matters.
    const Placement placement{place(image.getMemoryRequirements(), false)};
    image.bindMemory(placement.memory, placement.offset);
}

void* MemoryArena::bind(const vk::raii::Buffer& buffer)
{
    // Buffers are always linear, by definition rather than by choice.
    const Placement placement{place(buffer.getMemoryRequirements(), true)};
    buffer.bindMemory(placement.memory, placement.offset);
    return placement.mapped;
}

void MemoryArena::reset()
{
    m_blocks.clear();
}
