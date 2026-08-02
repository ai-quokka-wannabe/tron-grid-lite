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
#include <cstdint>
#include <vector>

class Device; // forward declaration

/*!
    Hands out slices of a few large device allocations instead of one allocation per resource.

    Vulkan's validation layer says the same thing on every run of this renderer, once per image:
    *"trying to bind to a memory block which is fully consumed by the image… smaller images like
    this should be sub-allocated from larger memory blocks"*. Nineteen of those on a Grid that has
    never allocated more than about eighteen blocks is not a memory problem — `maxMemoryAllocationCount`
    is typically 4,096, so the renderer sits at well under one per cent of the cap.

    **It is a signal-to-noise problem, and that is the reason this exists now.** Nineteen known-benign
    warnings on every run are nineteen places for a real one to hide, and validation output that is
    routinely ignored is validation that has stopped working. The memory argument arrives later, in
    Phase 6, when creature sensors multiply the count: many creatures, two eyes each, each eye a small
    render target far below the threshold.

    **A bump allocator, deliberately.** There is no free list and no per-resource release: an arena
    hands out offsets in order and reclaims everything at once when it is reset or destroyed. That is
    not a simplification of a real allocator, it is the right shape for what this renderer does —
    every group of resources here is created together and destroyed together. The bloom pyramid is
    rebuilt whole on resize; the output images are rebuilt whole on resize; the Grid's buffers live
    from upload to shutdown. Nothing ever wants to free one image and keep its neighbour, so a free
    list would be machinery serving no caller.

    Not thread-safe. Every caller allocates during construction or during a resize, both of which
    happen on one thread with the device idle.
*/
class MemoryArena {
public:
    /*!
        Creates an empty arena. No device memory is allocated until something asks for some.

        \param device Logical device.
        \param properties Memory properties every block must satisfy, such as device-local.
        \param block_bytes Size of each block. A resource larger than this gets a block of its own,
               sized to fit, so this is a granularity rather than a limit.
    */
    MemoryArena(const Device& device, vk::MemoryPropertyFlags properties, vk::DeviceSize block_bytes);

    // Non-copyable, non-movable: resources are bound to memory it owns.
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;
    MemoryArena(MemoryArena&&) = delete;
    MemoryArena& operator=(MemoryArena&&) = delete;

    /*!
        Binds an image to a slice of arena memory.

        \param image Image to bind. Must not already be bound.
        \throws std::runtime_error when no memory type satisfies both the image and the arena.
    */
    void bind(const vk::raii::Image& image);

    /*!
        Binds a buffer to a slice of arena memory and returns where the host can reach it.

        **A block is mapped at most once, by the arena, and that is why the address comes from
        here.** Vulkan forbids mapping one `VkDeviceMemory` twice, so two buffers sharing a block
        cannot each map it — the arena maps a host-visible block when it creates it and hands out
        interior pointers.

        \param buffer Buffer to bind. Must not already be bound.
        \return Address of this buffer's first byte, or `nullptr` if the arena is not host-visible.
        \throws std::runtime_error when no memory type satisfies both the buffer and the arena.
    */
    void* bind(const vk::raii::Buffer& buffer);

    /*!
        Frees every block, reclaiming all of it at once.

        **Every resource bound to this arena must already have been destroyed.** Freeing memory that
        an image is still bound to is undefined behaviour, and it is the one way to misuse this
        class. The callers that reset all do so from a `resize`, immediately after clearing the
        vectors that owned the images — which is the pattern to copy.
    */
    void reset();

    //! Returns how many device allocations this arena currently holds. Used by tests and diagnostics.
    [[nodiscard]] uint32_t blockCount() const
    {
        return static_cast<uint32_t>(m_blocks.size());
    }

private:
    //! One device allocation, handed out from the front.
    struct Block {
        uint32_t memory_type_index{0u}; //!< Which memory type this block was allocated from.
        vk::raii::DeviceMemory memory{nullptr}; //!< The allocation itself.
        vk::DeviceSize capacity{0u}; //!< Total size, in bytes.
        vk::DeviceSize used{0u}; //!< Bytes handed out so far.
        bool holds_linear{false}; //!< Whether the last resource placed was linear. See bufferImageGranularity.
        bool occupied{false}; //!< Whether anything at all has been placed yet.
        void* mapped{nullptr}; //!< Base address, if the arena is host-visible. Mapped once, when the block is made.
    };

    //! Where a resource should be bound.
    struct Placement {
        vk::DeviceMemory memory{}; //!< Block to bind to.
        vk::DeviceSize offset{0u}; //!< Offset within it.
        void* mapped{nullptr}; //!< Address of this slice, if the arena is host-visible.
    };

    /*!
        Finds or makes room for a resource and returns where it goes.

        \param requirements The resource's own memory requirements.
        \param linear Whether the resource has linear layout. Buffers always do; images do only with
               `vk::ImageTiling::eLinear`, which nothing here uses.
        \return Block and offset to bind at.
    */
    [[nodiscard]] Placement place(const vk::MemoryRequirements& requirements, bool linear);

    const Device* m_device{nullptr}; //!< Logical device (non-owning).
    vk::MemoryPropertyFlags m_properties{}; //!< Properties every block must have.
    vk::DeviceSize m_block_bytes{0u}; //!< Preferred block size.
    vk::DeviceSize m_buffer_image_granularity{1u}; //!< From the device; padding needed between a linear and a non-linear resource.

    std::vector<Block> m_blocks; //!< Every allocation this arena owns.
};
