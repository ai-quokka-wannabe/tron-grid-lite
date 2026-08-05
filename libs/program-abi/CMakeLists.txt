# A header and nothing else, so an INTERFACE target and no compile features. Deliberately neither
# cxx_std_20 nor a C standard: the whole point of the file is that it compiles as either language
# under any standard a Program's author happens to use, and a target that pinned one would quietly
# stop being true of the other.

add_library(program_abi INTERFACE)

target_include_directories(program_abi
    INTERFACE include/
)

add_subdirectory(tests)
