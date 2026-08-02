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

World::World(const Device& device, const BvhLib::Bvh& bvh, LoggingLib::Logger& logger) :
    m_node_count(static_cast<uint32_t>(bvh.nodes.size())),
    m_triangle_count(static_cast<uint32_t>(bvh.triangles.size()))
{
    // A storage buffer of size zero is not legal, so an empty Grid still gets one element of each.
    // Every shader checks the node count and misses rather than reading them.
    const BvhLib::Node placeholder_node{};
    const BvhLib::Triangle placeholder_triangle{};

    m_nodes = VulkanHelpers::uploadStorageBuffer(device, bvh.nodes.empty() ? static_cast<const void*>(&placeholder_node) : static_cast<const void*>(bvh.nodes.data()),
        bvh.nodes.empty() ? sizeof(BvhLib::Node) : (bvh.nodes.size() * sizeof(BvhLib::Node)));

    m_triangles = VulkanHelpers::uploadStorageBuffer(device,
        bvh.triangles.empty() ? static_cast<const void*>(&placeholder_triangle) : static_cast<const void*>(bvh.triangles.data()),
        bvh.triangles.empty() ? sizeof(BvhLib::Triangle) : (bvh.triangles.size() * sizeof(BvhLib::Triangle)));

    const vk::DeviceSize total_bytes{
        (static_cast<vk::DeviceSize>(m_node_count) * sizeof(BvhLib::Node)) + (static_cast<vk::DeviceSize>(m_triangle_count) * sizeof(BvhLib::Triangle))};

    logger.logInfo("Grid uploaded: " + std::to_string(m_triangle_count) + " triangles, " + std::to_string(m_node_count) + " hierarchy nodes, "
        + std::to_string(total_bytes / 1024u) + " KiB of device-local storage.");
}
