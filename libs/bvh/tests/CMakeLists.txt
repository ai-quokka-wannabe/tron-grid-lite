add_executable(bvh_tests bvh_tests.cpp)
target_link_libraries(bvh_tests PRIVATE bvh testing)
add_test(NAME bvh_tests COMMAND bvh_tests)
