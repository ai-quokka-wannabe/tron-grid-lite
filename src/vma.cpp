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

/*
    Sole translation unit that instantiates the Vulkan Memory Allocator implementation.

    The build MUST define VMA_STATIC_VULKAN_FUNCTIONS=0 and VMA_DYNAMIC_VULKAN_FUNCTIONS=0 for
    this target. VK_NO_PROTOTYPES is set globally and Vulkan is loaded dynamically via Volk, so
    VMA must not attempt to reference statically linked entry points nor resolve them itself.
    Allocator's constructor hands VMA an explicit function-pointer table built by
    vmaImportVulkanFunctionsFromVolk().
*/

// Suppress ALL warnings from third-party VMA header — we cannot modify it.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

// The Vulkan SDK installs the header as Volk/volk.h on Windows and volk/volk.h elsewhere.
// Clang-cl treats the case mismatch as -Wnonportable-include-path, which -Werror turns fatal.
#ifdef _WIN32
#include <Volk/volk.h>
#else
#include <volk/volk.h>
#endif

// Guards match those in allocator.hpp — see the explanation there.
#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#endif
#ifndef VMA_DYNAMIC_VULKAN_FUNCTIONS
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#endif

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
