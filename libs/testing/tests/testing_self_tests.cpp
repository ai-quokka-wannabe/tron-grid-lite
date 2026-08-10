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
#include <cstddef>
#include <limits>
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

TEST_CASE(check_equal_mixed_signedness_compares_by_value)
{
    // No cast: mixed-sign integer pairs go through std::cmp_equal, so this neither warns under
    // /W4 and -Wall nor converts either side.
    TEST_CHECK_EQUAL(std::size_t{3}, 3);
    TEST_CHECK_EQUAL(3, std::size_t{3});
    TEST_CHECK_EQUAL(-5, -5);
}

TEST_CASE(check_close_within_tolerance)
{
    TEST_CHECK_CLOSE(1.0f, 1.0001f, 1e-3f);
    TEST_CHECK_CLOSE(2.0, 2.0, 1e-12);
    TEST_CHECK_CLOSE(-3.5f, -3.5f, 1e-6f);
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

TEST_CASE(the_check_equal_macro_actually_fails_across_signedness)
{
    /*
        The discriminating case for std::cmp_equal: under the arithmetic conversions -1 becomes
        UINT_MAX and `-1 == static_cast<unsigned>(-1)` is true, so a naive == here would pass the
        one comparison the by-value semantics exist to fail.
    */
    bool threw{false};
    try {
        TEST_CHECK_EQUAL(-1, static_cast<unsigned int>(-1));
    } catch (const std::runtime_error&) {
        threw = true;
    }

    if (!threw) {
        reportUnenforced("TEST_CHECK_EQUAL(-1, UINT_MAX) did not fail");
    }
}

TEST_CASE(the_check_close_macro_actually_fails_when_apart)
{
    bool threw{false};
    try {
        TEST_CHECK_CLOSE(1.0f, 2.0f, 1e-3f);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    if (!threw) {
        reportUnenforced("TEST_CHECK_CLOSE(1.0f, 2.0f, 1e-3f) did not fail");
    }
}

TEST_CASE(the_check_close_macro_actually_fails_on_nan)
{
    // NaN compares false against everything, so `difference >= tolerance` would wave it through;
    // this pins the `!(difference < tolerance)` formulation.
    bool threw{false};
    try {
        TEST_CHECK_CLOSE(std::numeric_limits<float>::quiet_NaN(), 0.0f, 1e-3f);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    if (!threw) {
        reportUnenforced("TEST_CHECK_CLOSE on NaN did not fail");
    }
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
