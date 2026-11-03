# Copyright (C) 2026 The AiQuokkaWannabe Authors <https://github.com/ai-quokka-wannabe/tron-grid-lite>
#
# This program is free software: you can redistribute it and/or modify it under the terms of
# the GNU General Public License as published by the Free Software Foundation, either version
# 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program.
# If not, see <https://www.gnu.org/licenses/>.

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
