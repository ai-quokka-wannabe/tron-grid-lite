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

#include "testing/testing.hpp"
#include <stdexcept>
#include <string>

TEST_CASE(check_true_passes)
{
    TEST_CHECK(true);
    TEST_CHECK(1 == 1);
    TEST_CHECK(42 > 0);
}

TEST_CASE(check_equal_same_values)
{
    TEST_CHECK_EQUAL(1, 1);
    TEST_CHECK_EQUAL(0, 0);
    TEST_CHECK_EQUAL(-5, -5);
}

TEST_CASE(check_throws_catches_exception)
{
    TEST_CHECK_THROWS(throw std::runtime_error("expected"));
    TEST_CHECK_THROWS(throw 42);
}

/*
    The three cases below exist because everything above them passes whether or not the assertions
    do anything at all. Redefine TEST_CHECK as `do {} while (false)` and every one of this
    repository's tests goes green while checking nothing — which is the worst failure a test suite
    can have, because it is silent and it makes every other suite worthless.

    They therefore report failure by throwing directly rather than through TEST_CHECK. A meta-test
    that asserted its conclusion with the very macro it is testing would be neutered by the same
    edit it exists to catch. The runner treats any escaping std::exception as a failure, so this
    reports correctly while depending on none of the machinery under test.
*/

//! Fails the current test unconditionally, without going through any assertion macro.
namespace
{
    [[noreturn]] void reportUnenforced(const char* what)
    {
        throw std::logic_error{std::string{"assertions are not enforcing anything: "} + what};
    }
}

TEST_CASE(the_check_macro_actually_fails_on_a_false_expression)
{
    bool threw{false};
    try {
        TEST_CHECK(1 == 2);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    if (!threw) {
        reportUnenforced("TEST_CHECK(1 == 2) did not fail");
    }
}

TEST_CASE(the_check_equal_macro_actually_fails_on_unequal_values)
{
    bool threw{false};
    try {
        TEST_CHECK_EQUAL(1, 2);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    if (!threw) {
        reportUnenforced("TEST_CHECK_EQUAL(1, 2) did not fail");
    }
}

TEST_CASE(the_check_throws_macro_actually_fails_when_nothing_throws)
{
    bool threw{false};
    try {
        TEST_CHECK_THROWS((void)0);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    if (!threw) {
        reportUnenforced("TEST_CHECK_THROWS on a non-throwing expression did not fail");
    }
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
