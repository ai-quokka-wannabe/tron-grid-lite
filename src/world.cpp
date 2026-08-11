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

#include "world.hpp"
#include "device.hpp"
#include <string>

namespace
{

    //! Block size for the buffer arena. Comfortably larger than every table this owns put together.
    constexpr vk::DeviceSize BUFFER_BLOCK_BYTES{4u * 1024u * 1024u};

    /*!
        A lone hierarchy as a one-placement flat scene.

        The Grid is an instance like any other, and it goes through `makeInstance` and `flatten`
        rather than round the side of them. That is the point: the path a creature body takes is
        the path the only body in the world took for every frame this renderer had drawn before
        bodies existed, instead of a path a test promised worked.

        An identity placement also makes the two-level traversal free to verify: one instance at
        the identity must produce, to the bit, the picture that traversing the geometry directly
        produces — see the reference render digests in `.claude/CLAUDE.md`.
    */
    [[nodiscard]] BvhLib::FlatScene singlePlacement(const BvhLib::Bvh& bvh, const MathLib::Mat4& to_world)
    {
        BvhLib::Scene scene{};
        scene.geometries.push_back(bvh);
        scene.instances.push_back(BvhLib::makeInstance(bvh, 0u, to_world));
        return BvhLib::flatten(scene);
    }

} // namespace

World::World(const Device& device, const BvhLib::Bvh& bvh, LoggingLib::Logger& logger, const MathLib::Mat4& to_world) :
    World(device, singlePlacement(bvh, to_world), logger)
{
}

World::World(const Device& device, const BvhLib::FlatScene& flat, LoggingLib::Logger& logger) :
    m_arena(device, vk::MemoryPropertyFlagBits::eDeviceLocal, BUFFER_BLOCK_BYTES)
{
    m_node_count = static_cast<uint32_t>(flat.nodes.size());
    m_triangle_count = static_cast<uint32_t>(flat.triangles.size());
    m_instance_count = static_cast<uint32_t>(flat.instances.size());

    // A storage buffer of size zero is not legal, so an empty Grid still gets one element of each.
    // Every shader checks the node count and misses rather than reading them. The instance buffer
    // needs no such treatment: an empty Grid still has a placement, whose node count is zero.
    const BvhLib::Node placeholder_node{};
    const BvhLib::Triangle placeholder_triangle{};

    m_nodes = VulkanHelpers::uploadStorageBuffer(device, m_arena,
        flat.nodes.empty() ? static_cast<const void*>(&placeholder_node) : static_cast<const void*>(flat.nodes.data()),
        flat.nodes.empty() ? sizeof(BvhLib::Node) : (flat.nodes.size() * sizeof(BvhLib::Node)));

    m_triangles = VulkanHelpers::uploadStorageBuffer(device, m_arena,
        flat.triangles.empty() ? static_cast<const void*>(&placeholder_triangle) : static_cast<const void*>(flat.triangles.data()),
        flat.triangles.empty() ? sizeof(BvhLib::Triangle) : (flat.triangles.size() * sizeof(BvhLib::Triangle)));

    m_instances = VulkanHelpers::uploadStorageBuffer(device, m_arena, flat.instances.data(), flat.instances.size() * sizeof(BvhLib::InstanceRecord));

    const vk::DeviceSize total_bytes{(static_cast<vk::DeviceSize>(m_node_count) * sizeof(BvhLib::Node))
        + (static_cast<vk::DeviceSize>(m_triangle_count) * sizeof(BvhLib::Triangle)) + (static_cast<vk::DeviceSize>(m_instance_count) * sizeof(BvhLib::InstanceRecord))};

    logger.logInfo("Grid uploaded: " + std::to_string(m_triangle_count) + " triangles, " + std::to_string(m_node_count) + " hierarchy nodes, "
        + std::to_string(m_instance_count) + " instance" + ((m_instance_count == 1u) ? "" : "s") + ", " + std::to_string(total_bytes / 1024u)
        + " KiB of device-local storage.");
}
