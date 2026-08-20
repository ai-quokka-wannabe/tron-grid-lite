cmake_minimum_required(VERSION 3.25)

# C is enabled for one reason: the Program ABI is a C header, and a check that only ever compiles it
# as C++ does not check the thing Programs are written against. It buys the header compiled as C17
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

# Link — the wire of the Grid — arrives as a source submodule and is built by its own toolchain:
# cargo produces the shared library the Grid loads at run time, exactly as it loads Programs.
# Nothing here links against it; the C ABI behind one LoadLibrary'd symbol is the whole coupling,
# which is what lets an MSVC-built Grid, a MinGW-built Grid and the cargo-built library all agree.
find_program(TGL_CARGO_EXECUTABLE cargo REQUIRED)

set(TGL_LINK_SUBMODULE_DIR ${CMAKE_SOURCE_DIR}/external/link)
if(NOT EXISTS ${TGL_LINK_SUBMODULE_DIR}/Cargo.toml)
    message(FATAL_ERROR "The Link submodule is empty. Run: git submodule update --init")
endif()

if(WIN32)
    set(TGL_LINK_LIBRARY_FILE ${TGL_LINK_SUBMODULE_DIR}/target/release/link.dll)
else()
    set(TGL_LINK_LIBRARY_FILE ${TGL_LINK_SUBMODULE_DIR}/target/release/liblink.so)
endif()

# cargo is its own dependency tracker and a fraction of a second when nothing changed, so the
# target simply always runs it — the shaders' ALL-target reasoning, with cargo as the tracker.
add_custom_target(link_library ALL
    COMMAND ${TGL_CARGO_EXECUTABLE} build --release
    WORKING_DIRECTORY ${TGL_LINK_SUBMODULE_DIR}
    COMMENT "Building Link (cargo build --release)"
    VERBATIM
)

enable_testing()

add_subdirectory(libs)
add_subdirectory(src)
