cmake_minimum_required(VERSION 3.25)

# C is enabled for one reason: the Program ABI is a C header, and a check that only ever compiles it
# as C++ does not check the thing Programs are written against. It buys the C99 assertion fallback
# and a Program built as a shared library, neither of which C++ can reach.
project(TronGridLite
    LANGUAGES CXX C
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
    #
    # /EHsc is C++ only. C has no exceptions to model, and MSVC objects to being told about them.
    add_compile_options(/W4 /WX /utf-8 $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(NOT (WIN32 OR (UNIX AND NOT APPLE)))
    message(FATAL_ERROR "Unsupported platform. Only Windows and Linux are supported.")
endif()

# The Vulkan SDK, for its headers and nothing else.
#
# Volk resolves every entry point at run time and VK_NO_PROTOTYPES is defined for every target that
# touches Vulkan, so this project links no Vulkan library at all — deliberately, and it is a rule in
# .claude/CLAUDE.md. find_package(Vulkan REQUIRED) nonetheless insists on locating libvulkan, which
# is a dependency this build does not have; it went unnoticed for as long as every SDK happened to
# ship one where CMake looks. SDK 1.4.357.0 does not, on Linux, and the configure step failed on a
# library nothing would have linked.
#
# Asking for the headers says what is actually needed, and says it in one place.
find_path(TGL_VULKAN_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    HINTS $ENV{VULKAN_SDK}/include
    REQUIRED
)

message(STATUS "Vulkan headers: ${TGL_VULKAN_INCLUDE_DIR}")

enable_testing()

add_subdirectory(libs)
add_subdirectory(src)
