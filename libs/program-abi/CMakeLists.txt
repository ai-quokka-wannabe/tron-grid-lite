# A header and nothing else, so an INTERFACE target and no compile features. Deliberately neither
# cxx_std_20 nor a C standard: the whole point of the file is that it compiles as either language
# under any standard a Program's author happens to use, and a target that pinned one would quietly
# stop being true of the other.

add_library(program_abi INTERFACE)

target_include_directories(program_abi
    INTERFACE include/
)

add_subdirectory(tests)

# The layout, published as data. The header's static assertions protect a Program written in C or in
# a language that can consume a C header; every other binding declares the structs a second time and
# has nothing holding the copies together. This target is what those bindings can check against.
#
# Built as C11 so it can ask the compiler for _Alignof, and given the same offsets a Program sees.
add_executable(tgl_abi_manifest tools/abi_manifest.c)
target_link_libraries(tgl_abi_manifest PRIVATE program_abi)
set_target_properties(tgl_abi_manifest PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON)

# Microsoft deprecates fopen in favour of fopen_s, which is not a function the C standard has. Clang
# in clang-cl mode honours that deprecation and -Werror turns it into an error, while this repository
# would rather write portable C than one spelling per platform. Scoped to this target: it is the only
# C in the tree that opens a file, and the "unsafe" the warning means is about the scanf family
# rather than about opening something by name.
if(MSVC)
    target_compile_definitions(tgl_abi_manifest PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Regenerating is a deliberate act, so it is a target somebody runs rather than a step of the build:
# a manifest that silently rewrote itself would agree with the header by construction and check
# nothing at all.
add_custom_target(tgl_abi_manifest_write
    COMMAND tgl_abi_manifest --write "${CMAKE_CURRENT_SOURCE_DIR}/abi_layout.txt"
    COMMENT "Writing the ABI layout manifest"
    VERBATIM
)

add_test(NAME abi_layout_manifest COMMAND tgl_abi_manifest --check "${CMAKE_CURRENT_SOURCE_DIR}/abi_layout.txt")
