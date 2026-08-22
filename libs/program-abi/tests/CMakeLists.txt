# Compiling the header is most of the test, because every layout fact it depends on is a static
# assertion inside it. So each target below is a check, and the two of them cover the two
# combinations that exist: C17 (a Program, exported — the C standard C++20's own library baseline
# names, so both sides of the boundary quote one era of the language) and C++20 (the Grid's own
# side). Anything older meets the header's own #error rather than a fallback: there is nothing on
# the Grid for a pre-C11 toolchain, deliberately.

# A whole Program, as a shared library, because that is the only way to compile TGL_PROGRAM_EXPORT
# and the exported symbol at all — the Grid's build defines TGL_PROGRAM_IMPLEMENTATION nowhere. It is
# also the fixture the loader will need something to load.
add_library(tgl_test_program SHARED test_program.c)
target_link_libraries(tgl_test_program PRIVATE program_abi)
set_target_properties(tgl_test_program PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON)

# The Grid resolves an identifier to exactly one filename: `<name>.dll` on Windows, `lib<name>.so` on
# Linux. CMake agrees on Linux and disagrees on Windows under MinGW, which prefixes shared libraries
# with `lib` there too — so a fixture built with it lands as `libtgl_test_program.dll` and the loader
# correctly refuses to find it. Every Program has to satisfy this, so the fixtures satisfy it the
# same way a Program's own build would.
if(WIN32)
    set(TGL_PROGRAM_LIBRARY_PREFIX "")
else()
    set(TGL_PROGRAM_LIBRARY_PREFIX "lib")
endif()

set_target_properties(tgl_test_program PROPERTIES PREFIX "${TGL_PROGRAM_LIBRARY_PREFIX}")

# Programs that are wrong, one way each, so the loader's refusals have something to refuse. One
# source, one variant per target: the interesting part of each is the single line where it differs
# from a correct Program.
#
# They are built beside tgl_test_program because the loader resolves an identifier against a
# directory rather than opening a path, so a fixture is reachable only by being in one.
set(TGL_BROKEN_PROGRAM_VARIANTS
    no_symbol
    refuses_version
    wrong_version
    small_vtable
    null_library_init
    null_program_rez
    null_program_tick
    null_program_derez
    null_library_shutdown
)

foreach(variant IN LISTS TGL_BROKEN_PROGRAM_VARIANTS)
    string(TOUPPER "${variant}" variant_macro)
    add_library(tgl_broken_${variant} SHARED broken_program.c)
    target_link_libraries(tgl_broken_${variant} PRIVATE program_abi)
    target_compile_definitions(tgl_broken_${variant} PRIVATE TGL_BROKEN_${variant_macro})
    set_target_properties(tgl_broken_${variant} PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON)

    # Beside the good fixture, so one directory holds every Program the loader tests resolve, and
    # named the way the Grid resolves names rather than the way the toolchain would prefer.
    set_target_properties(tgl_broken_${variant} PROPERTIES
        PREFIX "${TGL_PROGRAM_LIBRARY_PREFIX}"
        LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:tgl_test_program>"
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:tgl_test_program>"
    )
endforeach()

# Programs that are correct and that drive a body, so the tick loop can be watched from outside
# rather than assumed: one asks for something a body can do, one turns while it walks, one calls
# every tick, one asks for far more than any body can, one asks for nothing at all, and one
# declines to rez.
set(TGL_DRIVER_PROGRAM_VARIANTS
    steady
    turning
    calling
    excessive
    silent
    refuses_rez
    refuses_second_rez
    modelled
    misshapen
)

foreach(variant IN LISTS TGL_DRIVER_PROGRAM_VARIANTS)
    string(TOUPPER "${variant}" variant_macro)
    add_library(tgl_driver_${variant} SHARED driver_program.c)
    target_link_libraries(tgl_driver_${variant} PRIVATE program_abi)
    target_compile_definitions(tgl_driver_${variant} PRIVATE TGL_DRIVER_${variant_macro})
    set_target_properties(tgl_driver_${variant} PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        PREFIX "${TGL_PROGRAM_LIBRARY_PREFIX}"
        LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:tgl_test_program>"
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:tgl_test_program>"
    )
endforeach()

# One name for "every Program fixture", so that a consumer in another directory depends on the set
# rather than on a list of target names it would have to be kept in step with.
add_custom_target(tgl_test_programs)
add_dependencies(tgl_test_programs tgl_test_program)
foreach(variant IN LISTS TGL_BROKEN_PROGRAM_VARIANTS)
    add_dependencies(tgl_test_programs tgl_broken_${variant})
endforeach()
foreach(variant IN LISTS TGL_DRIVER_PROGRAM_VARIANTS)
    add_dependencies(tgl_test_programs tgl_driver_${variant})
endforeach()

add_executable(program_abi_tests program_abi_tests.cpp)
target_link_libraries(program_abi_tests PRIVATE program_abi testing)
add_test(NAME program_abi_tests COMMAND program_abi_tests)

# Nothing loads it yet, so make it something the test suite cannot silently stop building.
add_dependencies(program_abi_tests tgl_test_program)
