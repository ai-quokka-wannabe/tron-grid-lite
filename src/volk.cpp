/*
    Copyright (C) 2026 The AiQuokkaWannabe Authors https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

// The Vulkan SDK installs the header as Volk/volk.h on Windows and volk/volk.h elsewhere.
// Clang-cl treats the case mismatch as -Wnonportable-include-path, which -Werror turns fatal.
#define VOLK_IMPLEMENTATION
#ifdef _WIN32
#include <Volk/volk.h>
#else
#include <volk/volk.h>
#endif
