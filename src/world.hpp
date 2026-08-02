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

#include "memory_arena.hpp"
#include "vulkan_helpers.hpp"
#include <bvh/bvh.hpp>
#include <log/logger.hpp>
#include <cstdint>

class Device; // forward declaration

/*!
    The Grid's geometry, resident on the device.

    Two storage buffers — the hierarchy and the triangles it indexes — and nothing else. There is
    deliberately no material table here and no image: a material is a property of a *sense*, not of
    the Grid, and the two senses disagree about what a surface is. Light wants a colour, an index of
    refraction and a transmission; sound wants an absorption coefficient and a source strength.
    Neither belongs in the other's table, and neither belongs here.

    This exists because the hierarchy has two readers. It used to live inside `Tracer` as three
    private members, which was correct while the renderer was the only thing that traced anything;
    the acoustic pass walks the very same hierarchy, and making sound reach through vision to find
    the Grid would have been exactly backwards. `libs/bvh/include/bvh/bvh.hpp` has claimed from the
    start that "the same hierarchy is intended to serve acoustic rays later, which is why nothing
    here is specific to light" — this class is where that claim is kept on the host side, as
    `grid_bvh.slang` keeps it on the device side.

    Immutable once built. Phase 6 is where that changes, because creatures move and a hierarchy over
    moving geometry is rebuilt rather than uploaded once — see Etape 10 in TODO.md.
*/
class World {
public:
    /*!
        Uploads a hierarchy to device-local memory.

        \param device Logical device.
        \param bvh Hierarchy to upload. May be empty, in which case every ray misses and both
               `nodeCount` and `triangleCount` report zero.
        \param logger Logger for the upload summary.
    */
    World(const Device& device, const BvhLib::Bvh& bvh, LoggingLib::Logger& logger);

    // Non-copyable, non-movable: it owns Vulkan objects that passes hold references to.
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    //! Returns the hierarchy node buffer, 32 bytes per node, element 0 the root.
    [[nodiscard]] vk::Buffer nodes() const
    {
        return *m_nodes.buffer;
    }

    //! Returns the triangle buffer, 48 bytes per triangle, in leaf order.
    [[nodiscard]] vk::Buffer triangles() const
    {
        return *m_triangles.buffer;
    }

    /*!
        Returns the number of nodes in the hierarchy.

        Zero means an empty Grid. Every shader that traverses this must check it and miss, because
        the buffer still holds one placeholder element — Vulkan does not allow a zero-sized buffer,
        so an empty Grid is represented by a count of zero rather than by an absent buffer.
    */
    [[nodiscard]] uint32_t nodeCount() const
    {
        return m_node_count;
    }

    //! Returns the number of triangles the hierarchy indexes.
    [[nodiscard]] uint32_t triangleCount() const
    {
        return m_triangle_count;
    }

private:
    uint32_t m_node_count{0u}; //!< Nodes in the hierarchy; zero means an empty Grid.
    uint32_t m_triangle_count{0u}; //!< Triangles the hierarchy indexes.

    //! One block behind both buffers, rather than one allocation each. Declared first so it outlives them.
    MemoryArena m_arena;

    VulkanHelpers::DeviceBuffer m_nodes; //!< Hierarchy nodes, 32 bytes each.
    VulkanHelpers::DeviceBuffer m_triangles; //!< Triangles in leaf order, 48 bytes each.
};
