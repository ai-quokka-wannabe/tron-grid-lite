# Compiling the header is most of the test, because every layout fact it depends on is a static
# assertion inside it. So each target below is a check, and the three of them cover the three
# combinations that differ: C99 (the assertion fallback), C11 (a Program, exported) and C++20 (the
# Grid's own side).

# C99 reaches TGL_STATIC_ASSERT's negative-array-size fallback, which nothing else here compiles.
# C_STANDARD_REQUIRED is deliberately left off: MSVC has no /std:c99, and its default C mode reaches
# the same fallback anyway by leaving __STDC_VERSION__ undefined. Demanding the exact flag would fail
# the configure step on Windows to gain nothing.
add_library(program_abi_c99 STATIC abi_c99_compile.c)
target_link_libraries(program_abi_c99 PRIVATE program_abi)
set_target_properties(program_abi_c99 PROPERTIES C_STANDARD 99)

# A whole Program, as a shared library, because that is the only way to compile TGL_PROGRAM_EXPORT
# and the exported symbol at all — the Grid's build defines TGL_PROGRAM_IMPLEMENTATION nowhere. It is
# also the fixture the loader will need something to load.
add_library(tgl_test_program SHARED test_program.c)
target_link_libraries(tgl_test_program PRIVATE program_abi)
set_target_properties(tgl_test_program PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

add_executable(program_abi_tests program_abi_tests.cpp)
target_link_libraries(program_abi_tests PRIVATE program_abi program_abi_c99 testing)
add_test(NAME program_abi_tests COMMAND program_abi_tests)

# Nothing loads it yet, so make it something the test suite cannot silently stop building.
add_dependencies(program_abi_tests tgl_test_program)
