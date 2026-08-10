/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

/*
    Passes only if a test body throwing something that is not a std::exception is recorded as an
    ordinary failure — the runner must reach its summary and return, not std::terminate.

    An exception escaping runAll() would propagate out of main and abort the process, which is the
    failure mode this repository refuses everywhere: remaining tests skipped, no summary printed,
    and an abnormal exit instead of a meaningful one. Reaching the return statement below at all is
    the property under test, so this binary reports success precisely when runAll() reports that a
    test failed.

    It cannot live inside testing_self_tests: proving the runner survives requires a test case that
    is genuinely failing, which would turn that suite red.
*/

#include "testing/testing.hpp"

TEST_CASE(throws_something_that_is_not_a_std_exception)
{
    throw 42;
}

TEST_CASE(runs_after_the_foreign_throw)
{
    TEST_CHECK(true);
}

int main()
{
    return TestingLib::runAll() ? 0 : 1;
}
