add_executable(testing_self_tests
    testing_self_tests.cpp
)

target_link_libraries(testing_self_tests PRIVATE testing)

add_test(NAME testing_self_tests COMMAND testing_self_tests)

# Two properties of the runner itself that no TEST_CASE can assert from the inside: an empty suite
# must fail rather than pass, and a non-std::exception escaping a test body must be recorded as a
# failure rather than terminating the process. Each binary passes exactly when runAll() reports
# failure — see the comment at the top of each file.
add_executable(testing_empty_suite_test
    testing_empty_suite_test.cpp
)

target_link_libraries(testing_empty_suite_test PRIVATE testing)

add_test(NAME testing_empty_suite_test COMMAND testing_empty_suite_test)

add_executable(testing_foreign_exception_test
    testing_foreign_exception_test.cpp
)

target_link_libraries(testing_foreign_exception_test PRIVATE testing)

add_test(NAME testing_foreign_exception_test COMMAND testing_foreign_exception_test)
