# Every deviceless target here links `grid`, the library the audit said these files had been
# working around: before it existed, each test target re-compiled the translation units it needed
# one by one, and the lists drifted as dependencies grew. The library is the single statement of
# what the Grid's deviceless core is; a test that needs a device has no business in this
# directory at all.

add_executable(geometry_tests geometry_tests.cpp)
target_link_libraries(geometry_tests PRIVATE grid testing)
add_test(NAME geometry_tests COMMAND geometry_tests)

# The acoustic gather traces the very same hierarchy the renderer does — which is the whole
# point of it.
add_executable(acoustics_tests acoustics_tests.cpp)
target_link_libraries(acoustics_tests PRIVATE grid testing)
add_test(NAME acoustics_tests COMMAND acoustics_tests)

# The SPIR-V reader is header-only and mentions no Vulkan type, so this target links testing
# alone — no grid, and above all no Vulkan SDK. Every test in the file exercises a failure, and
# failures are the code no other check in this repository can reach: the reference render walks
# the success path; nothing walks the others.
add_executable(spirv_tests spirv_tests.cpp)
target_link_libraries(spirv_tests PRIVATE testing)
add_test(NAME spirv_tests COMMAND spirv_tests)

# The Program loader's refusals are all reachable here, on a machine with no device at all.
#
# It needs the fixtures to exist before it runs, and it needs to be told where they are. The
# directory arrives as a compile definition from the target that produces them rather than as a
# string assembled here, so a fixture moving in the build tree cannot leave this test resolving
# nothing and reporting that everything was correctly refused.
add_executable(program_library_tests program_library_tests.cpp)
target_link_libraries(program_library_tests PRIVATE grid testing)
target_compile_definitions(program_library_tests PRIVATE TGL_TEST_PROGRAM_DIR="$<TARGET_FILE_DIR:tgl_test_program>")
add_dependencies(program_library_tests tgl_test_programs)
add_test(NAME program_library_tests COMMAND program_library_tests)

# A Program driving a body is a complete thing to check and needs no device, which is the whole
# reason the roster mentions no Vulkan.
add_executable(roster_tests roster_tests.cpp)
target_link_libraries(roster_tests PRIVATE grid testing)
target_compile_definitions(roster_tests PRIVATE TGL_TEST_PROGRAM_DIR="$<TARGET_FILE_DIR:tgl_test_program>")
add_dependencies(roster_tests tgl_test_programs)
add_test(NAME roster_tests COMMAND roster_tests)

# A creature hearing the Grid: the senses seam takes a Creature rather than a Roster, so the
# tests build the creature by hand and hold the source to the ABI's own promises.
add_executable(senses_tests senses_tests.cpp)
target_link_libraries(senses_tests PRIVATE grid testing)
add_test(NAME senses_tests COMMAND senses_tests)

# The stage is host arithmetic over the roster's models — scene assembly, one material table,
# per-tick placements — and the ear path walks exactly the scene it maintains.
add_executable(stage_tests stage_tests.cpp)
target_link_libraries(stage_tests PRIVATE grid testing)
add_test(NAME stage_tests COMMAND stage_tests)

# Link is loaded exactly as a Program is — a shared library found at run time behind one exported
# symbol — so its loader gets the same deviceless treatment ProgramLib's has. The library file
# and the recorded fingerprint arrive as compile definitions from Link's own face — the LNK_*
# variables — for the same reason the Program fixtures' directory does: a path assembled here
# could drift from the file the face actually builds.
add_executable(link_library_tests link_library_tests.cpp)
target_link_libraries(link_library_tests PRIVATE grid testing)
target_compile_definitions(link_library_tests PRIVATE
    TGL_LINK_LIBRARY_FILE="${LNK_LIBRARY_FILE}"
    TGL_LINK_FINGERPRINT_FILE="${LNK_FINGERPRINT_FILE}")
add_test(NAME link_library_tests COMMAND link_library_tests)
lnk_copy_beside(link_library_tests)

# The spectator's half of a world, tested against the server's half of the same loaded library:
# this target plays Master Control through Link's own vtable — the first C++ consumer of the
# server surface, and the very reason it exists. Deviceless throughout.
add_executable(world_client_tests world_client_tests.cpp)
target_link_libraries(world_client_tests PRIVATE grid testing)
add_test(NAME world_client_tests COMMAND world_client_tests)
lnk_copy_beside(world_client_tests)

# The creature host's half of a world, against the same loaded library playing Master Control:
# REZ out, the telling and the owner's letter in, ACTIONS back. Deviceless - the roster ticks
# with no traced senses, and needs the Program fixtures to rez.
add_executable(world_host_tests world_host_tests.cpp)
target_link_libraries(world_host_tests PRIVATE grid testing)
target_compile_definitions(world_host_tests PRIVATE TGL_TEST_PROGRAM_DIR="$<TARGET_FILE_DIR:tgl_test_program>")
add_dependencies(world_host_tests tgl_test_programs)
add_test(NAME world_host_tests COMMAND world_host_tests)
lnk_copy_beside(world_host_tests)

# The live view's scene is host arithmetic like the stage's: what the tracer's dynamic instance
# path is fed is plain records, checkable with no device.
add_executable(world_stage_tests world_stage_tests.cpp)
target_link_libraries(world_stage_tests PRIVATE grid testing)
add_test(NAME world_stage_tests COMMAND world_stage_tests)

# The spectator's ears without speakers: the mixer rendered into a buffer and measured - pan,
# distance, Doppler counted in zero crossings, silence, retirement. Deviceless.
add_executable(audio_tests audio_tests.cpp)
target_link_libraries(audio_tests PRIVATE grid testing)
add_test(NAME audio_tests COMMAND audio_tests)
