# geometry.cpp is compiled into the renderer executable rather than into a library of its own, so
# the test target compiles the one translation unit it needs directly. Extracting a library for a
# single consumer would be an abstraction without a second use case.
add_executable(geometry_tests geometry_tests.cpp ../geometry.cpp)
target_link_libraries(geometry_tests PRIVATE math testing)
add_test(NAME geometry_tests COMMAND geometry_tests)

# Same reasoning for the acoustic model: acoustics.cpp is part of the renderer executable, and the
# test target compiles that one translation unit directly rather than a library extracted for a
# single consumer. It needs bvh as well, because the gather traces the very same hierarchy the
# renderer does — which is the whole point of it.
add_executable(acoustics_tests acoustics_tests.cpp ../acoustics.cpp ../geometry.cpp)
target_link_libraries(acoustics_tests PRIVATE math bvh testing)
add_test(NAME acoustics_tests COMMAND acoustics_tests)

# The SPIR-V reader is header-only and mentions no Vulkan type, so this target links testing alone —
# no math, no bvh, and above all no Vulkan SDK. That is the point of it rather than an economy: every
# test in the file exercises a failure, and failures are the code no other check in this repository
# can reach. The reference render walks the success path; nothing walks the others.
add_executable(spirv_tests spirv_tests.cpp)
target_link_libraries(spirv_tests PRIVATE testing)
add_test(NAME spirv_tests COMMAND spirv_tests)

# The Program loader mentions no Vulkan either, for the same reason and to the same benefit: every
# refusal it makes is reachable here, on a machine with no device at all.
#
# It needs the fixtures to exist before it runs, and it needs to be told where they are. The
# directory arrives as a compile definition from the target that produces them rather than as a
# string assembled here, so a fixture moving in the build tree cannot leave this test resolving
# nothing and reporting that everything was correctly refused.
add_executable(program_library_tests program_library_tests.cpp ../program_library.cpp ../loader_os.cpp)
target_link_libraries(program_library_tests PRIVATE program_abi testing ${CMAKE_DL_LIBS})
target_compile_definitions(program_library_tests PRIVATE TGL_TEST_PROGRAM_DIR="$<TARGET_FILE_DIR:tgl_test_program>")
add_dependencies(program_library_tests tgl_test_programs)
add_test(NAME program_library_tests COMMAND program_library_tests)

# A Program driving a body is a complete thing to check and needs no device, which is the whole
# reason the roster mentions no Vulkan. It compiles the two translation units it needs directly,
# for the same reason geometry_tests does: extracting a library for one consumer would be an
# abstraction without a second use case.
add_executable(roster_tests roster_tests.cpp ../roster.cpp ../program_library.cpp ../loader_os.cpp)
target_link_libraries(roster_tests PRIVATE program_abi math bvh testing ${CMAKE_DL_LIBS})
target_compile_definitions(roster_tests PRIVATE TGL_TEST_PROGRAM_DIR="$<TARGET_FILE_DIR:tgl_test_program>")
add_dependencies(roster_tests tgl_test_programs)
add_test(NAME roster_tests COMMAND roster_tests)

# A creature hearing the Grid is likewise a complete thing to check with no device: the senses seam
# takes a Creature rather than a Roster, so no Program library is needed either — the tests build
# the creature by hand and hold the source to the ABI's own promises about what it wires in.
add_executable(senses_tests senses_tests.cpp ../senses.cpp ../stage.cpp ../roster.cpp ../program_library.cpp ../loader_os.cpp ../acoustics.cpp)
target_link_libraries(senses_tests PRIVATE program_abi math bvh testing ${CMAKE_DL_LIBS})
add_test(NAME senses_tests COMMAND senses_tests)

# The stage is host arithmetic over the roster's models — scene assembly, one material table,
# per-tick placements — so it is testable with no device, which is where its correctness matters:
# the ear path walks exactly the scene this class maintains.
add_executable(stage_tests stage_tests.cpp ../stage.cpp ../roster.cpp ../program_library.cpp ../loader_os.cpp ../acoustics.cpp)
target_link_libraries(stage_tests PRIVATE program_abi math bvh testing ${CMAKE_DL_LIBS})
add_test(NAME stage_tests COMMAND stage_tests)

# Link is loaded exactly as a Program is — a shared library found at run time behind one exported
# symbol — so its loader gets the same deviceless treatment ProgramLib's has. Compiling this
# target is itself half the etape: lnk_protocol.h and lnk_client.h meet a C++ compiler here, so
# their static asserts finally run, on every preset, under /WX and -Werror. The library file and
# the recorded fingerprint arrive as compile definitions from Link's own face — the LNK_*
# variables — for the same reason the Program fixtures' directory does: a path assembled here
# could drift from the file the face actually builds.
add_executable(link_library_tests link_library_tests.cpp ../link_library.cpp ../loader_os.cpp)
target_link_libraries(link_library_tests PRIVATE lnk testing ${CMAKE_DL_LIBS})
target_compile_definitions(link_library_tests PRIVATE
    TGL_LINK_LIBRARY_FILE="${LNK_LIBRARY_FILE}"
    TGL_LINK_FINGERPRINT_FILE="${LNK_FINGERPRINT_FILE}")
add_test(NAME link_library_tests COMMAND link_library_tests)
lnk_copy_beside(link_library_tests)
