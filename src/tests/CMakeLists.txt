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
add_executable(acoustics_tests acoustics_tests.cpp ../acoustics.cpp)
target_link_libraries(acoustics_tests PRIVATE math bvh testing)
add_test(NAME acoustics_tests COMMAND acoustics_tests)
