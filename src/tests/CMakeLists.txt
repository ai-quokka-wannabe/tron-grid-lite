# geometry.cpp is compiled into the renderer executable rather than into a library of its own, so
# the test target compiles the one translation unit it needs directly. Extracting a library for a
# single consumer would be an abstraction without a second use case.
add_executable(geometry_tests geometry_tests.cpp ../geometry.cpp)
target_link_libraries(geometry_tests PRIVATE math testing)
add_test(NAME geometry_tests COMMAND geometry_tests)
