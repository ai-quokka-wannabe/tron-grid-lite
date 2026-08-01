cmake_minimum_required(VERSION 3.25)

project(TronGridLite
    LANGUAGES CXX
    VERSION 0.1.0
    DESCRIPTION "Simplified Vulkan-based Tron Grid renderer for AI creatures"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# STYLE.md described a clang-tidy error gate that nothing executed — no CI step, no CMake
# integration, no pre-commit hook. Off by default because it roughly doubles build time; the CI
# quick-checks job turns it on, which is what makes the claim in STYLE.md true.
option(TGL_ENABLE_CLANG_TIDY "Run clang-tidy as part of the build" OFF)

if(TGL_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
endif()

# Compiler warnings — high level, warnings as errors, all compilers
if(MSVC)
    # /utf-8 because the sources are UTF-8 without a BOM and MSVC would otherwise decode them in the
    # active code page — which on this project's own development machine is Windows-1250, and which
    # has silently mangled em dashes in these files before.
    add_compile_options(/EHsc /W4 /WX /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(NOT (WIN32 OR (UNIX AND NOT APPLE)))
    message(FATAL_ERROR "Unsupported platform. Only Windows and Linux are supported.")
endif()

# Vulkan SDK — provides Volk, vulkan-hpp, and validation layers
find_package(Vulkan REQUIRED)

enable_testing()

add_subdirectory(libs)
add_subdirectory(src)
