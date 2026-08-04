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

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

/*!
    Reading a compiled shader off disk, and refusing anything that is not one.

    **Nothing here mentions Vulkan**, and that is what the file is for. Loading a shader is file I/O
    and four validity questions; the answers are the same whether a device exists or not. Sitting
    behind a Vulkan header would mean a test of those four answers needed the Vulkan SDK to compile,
    which is how error paths end up untested — and error paths are the code that only runs when
    something has already gone wrong, which is exactly when it had better be right.
*/
namespace SpirvLib
{

    //! First word of every SPIR-V module, little-endian.
    inline constexpr uint32_t SPIRV_MAGIC{0x07230203u};

    /*!
        Reads a SPIR-V module and returns its words.

        Four ways this refuses, and each one is a mistake somebody actually makes: the path is wrong,
        the file is not a whole number of 32-bit words, the read came up short, or the file is
        perfectly valid and simply is not SPIR-V.

        \param path Path to a compiled `.spv` module.
        \return The module's words, at least one.
        \throws std::runtime_error with the path in the message, for any of the four.
    */
    [[nodiscard]] inline std::vector<uint32_t> read(const std::string& path)
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

} // namespace SpirvLib
