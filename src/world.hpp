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

    Three storage buffers — the hierarchies, the triangles they index, and the placements of them —
    and nothing else. There is deliberately no material table here and no image: a material is a
    property of a *sense*, not of the Grid, and the two senses disagree about what a surface is. Light
    wants a colour, an index of refraction and a transmission; sound wants an absorption coefficient
    and a source strength. Neither belongs in the other's table, and neither belongs here.

    This exists because the hierarchy has two readers. Holding it inside `Tracer` as three private
    members would be right only if the renderer were the only thing that traced anything; the
    acoustic pass walks the very same hierarchy, and making sound reach through vision to find the
    Grid would be exactly backwards. `libs/bvh/include/bvh/bvh.hpp` states that "the same hierarchy
    is intended to serve acoustic rays later, which is why nothing here is specific to light" — this
    class is where that claim is kept on the host side, as `grid_bvh.slang` keeps it on the device
    side.

    Immutable once built, and **it stays that way even when creatures move**. Phase 6 adds bodies, but
    they do not join this hierarchy: they get their own, under the top level that already exists here
    and today holds a single box — the Grid's, at the identity. The Grid's own structure is therefore
    never rebuilt, and a rigid body's is built once when it is rezzed. Rebuilding everything each tick
    would cost 31 ms with twenty creatures against 0.0031 ms for the top level alone — see
    `docs/ARCHITECTURE.md` § One hierarchy today, two when creatures move.

    That the one instance sits at the identity is what makes the arrangement cheap to trust: an
    identity transform must leave the picture bit-for-bit what traversing the geometry directly
    yields, so any arithmetic the top level gets wrong shows up as a changed reference digest.
*/
class World {
public:
    /*!
        Uploads a hierarchy to device-local memory as a single instance.

        \param device Logical device.
        \param bvh Hierarchy to upload. May be empty, in which case every ray misses and both
               `nodeCount` and `triangleCount` report zero.
        \param logger Logger for the upload summary.
        \param to_world Where to place it. The identity for the Grid, which is what every ordinary
               run uses. **A non-identity value exists so that the device can be made to trace one**,
               because otherwise nothing here ever exercises a transform on the GPU and the whole
               two-level traversal is verified only in the one configuration where it cannot be
               wrong — see `--verify-scene`.
    */
    World(const Device& device, const BvhLib::Bvh& bvh, LoggingLib::Logger& logger, const MathLib::Mat4& to_world = MathLib::Mat4::identity());

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
        Returns the instance buffer, 144 bytes per placement.

        The top level. Today it holds exactly one entry — the Grid, at the identity — and a creature
        body will be another. See `BvhLib::InstanceRecord`.
    */
    [[nodiscard]] vk::Buffer instances() const
    {
        return *m_instances.buffer;
    }

    //! Returns the number of placements in the top level. Never zero: an empty Grid is still placed.
    [[nodiscard]] uint32_t instanceCount() const
    {
        return m_instance_count;
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
    uint32_t m_node_count{0u}; //!< Nodes across every geometry; zero means an empty Grid.
    uint32_t m_triangle_count{0u}; //!< Triangles the hierarchies index.
    uint32_t m_instance_count{0u}; //!< Placements in the top level.

    //! One block behind all three buffers, rather than one allocation each. Declared first so it outlives them.
    MemoryArena m_arena;

    VulkanHelpers::DeviceBuffer m_nodes; //!< Hierarchy nodes, 32 bytes each, every geometry concatenated.
    VulkanHelpers::DeviceBuffer m_triangles; //!< Triangles in leaf order, 48 bytes each, in the same order.
    VulkanHelpers::DeviceBuffer m_instances; //!< Placements, 144 bytes each.
};
