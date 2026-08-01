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

#include <vulkan/vulkan_raii.hpp>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

/*!
    The two Vulkan chores every pass needs.

    Both of these existed three times over — byte-for-byte in the tracer and the post-processing
    stage, and a third time in `main.cpp` with a different name and a different error message. Three
    copies of a function is three places to fix a bug in, and the SPIR-V reader had exactly such a
    bug: it ignored the result of its own read, and only one copy would have been noticed.
*/
namespace VulkanHelpers
{

    //! First word of every SPIR-V module, per the specification.
    inline constexpr uint32_t SPIRV_MAGIC{0x07230203u};

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
        Reads a compiled SPIR-V module from disk.

        \param path Absolute path to the `.spv` file.
        \return The module's words.
        \throws std::runtime_error when the file cannot be opened, has a size that is not a whole
                number of words, is read short, or does not begin with the SPIR-V magic number.
    */
    [[nodiscard]] inline std::vector<uint32_t> readSpirv(const std::string& path)
    {
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file.is_open()) {
            throw std::runtime_error{"Failed to open SPIR-V module: " + path};
        }

        const std::streamsize size_bytes{file.tellg()};
        if ((size_bytes <= 0) || ((size_bytes % 4) != 0)) {
            throw std::runtime_error{"SPIR-V module has an invalid size: " + path};
        }

        std::vector<uint32_t> words(static_cast<size_t>(size_bytes) / 4u);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(words.data()), size_bytes);

        /*
            The read has to be checked. std::vector value-initialises, so a short read leaves a
            silently zero-filled tail while the full size is still reported to vkCreateShaderModule.
            A release build has no validation layer to reject the result, so the driver's SPIR-V
            parser consumes the zeros — a hang or a crash instead of the clean error this function
            is otherwise built to produce.
        */
        if (file.gcount() != size_bytes) {
            throw std::runtime_error{"Truncated SPIR-V module: " + path};
        }

        // The magic number catches the likelier mistake of pointing at the wrong file entirely.
        if (words.front() != SPIRV_MAGIC) {
            throw std::runtime_error{"Not a SPIR-V module: " + path};
        }

        return words;
    }

} // namespace VulkanHelpers
