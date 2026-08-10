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

#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <limits>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace TestingLib
{

    //! A registered test case with a name and callable body.
    struct TestCase {
        std::string_view name; //!< Test name.
        std::function<void()> fn; //!< Test body.
    };

    //! Returns the global list of registered test cases.
    std::vector<TestCase>& registry();

    //! Registers a test case; called automatically by TEST_CASE macro.
    void registerTest(std::string_view name, std::function<void()> fn);

    //! Runs all registered tests; returns true if any test failed, false if all passed.
    [[nodiscard]] bool runAll();

    //! Reports a check failure; throws to abort the current test.
    [[noreturn]] void checkFailed(std::string_view expr, std::source_location loc = std::source_location::current());

    //! Reports an equality check failure; throws to abort the current test.
    [[noreturn]] void checkEqualFailed(std::string_view lhs_expr, std::string_view rhs_expr, std::string_view lhs_val, std::string_view rhs_val,
        std::source_location loc = std::source_location::current());

    //! Reports a closeness check failure; throws to abort the current test.
    [[noreturn]] void checkCloseFailed(std::string_view lhs_expr, std::string_view rhs_expr, std::string_view lhs_val, std::string_view rhs_val,
        std::string_view diff_val, std::string_view tolerance_val, std::source_location loc = std::source_location::current());

    // ---------------------------------------------------------------------------
    // Template helpers — real logic lives here, not in macros
    // ---------------------------------------------------------------------------

    //! Converts a value to a string for diagnostics. Resolution order:
    //! - Anything convertible to std::string (so const char*, std::string_view, etc.).
    //! - Floating-point types at max_digits10, so the text round-trips the exact value.
    //! - Remaining built-in arithmetic types via std::to_string.
    //! - Project / user types that provide an ADL-discoverable to_string overload —
    //!   e.g. `std::string to_string(const MyType&)` in MyType's namespace lets
    //!   TEST_CHECK_EQUAL print informative diagnostics for failed comparisons of
    //!   project types instead of the "<?>" fallback.
    //! - Fallback: "<?>".
    //!
    //! The ADL lookup uses a `using std::to_string` declaration so it picks up both
    //! standard-library overloads and any user-provided overload found through
    //! argument-dependent lookup.
    template <typename T> [[nodiscard]] std::string toString(const T& v)
    {
        if constexpr (std::is_convertible_v<T, std::string>) {
            return std::string(v);
        } else if constexpr (std::is_floating_point_v<std::decay_t<T>>) {
            // Not std::to_string, whose fixed six decimals cannot even express the difference a
            // tight tolerance is measuring: at max_digits10 the text round-trips the exact value.
            std::ostringstream oss;
            oss << std::setprecision(std::numeric_limits<std::decay_t<T>>::max_digits10) << v;
            return oss.str();
        } else if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
            return std::to_string(v);
        } else if constexpr (requires(const T& x) {
                                 { to_string(x) } -> std::convertible_to<std::string>;
                             }) {
            using std::to_string; // Brings std overloads into scope; ADL finds user ones.
            return to_string(v);
        } else {
            return "<?>";
        }
    }

    //! True for the integer types std::cmp_equal accepts: bool and the character types are excluded
    //! by the standard, everything else integral is in.
    template <typename T>
    inline constexpr bool is_comparable_integer_v = std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, wchar_t>
        && !std::is_same_v<T, char8_t> && !std::is_same_v<T, char16_t> && !std::is_same_v<T, char32_t>;

    //! Checks that two values are equal; reports failure with stringified expressions and values.
    //!
    //! Two integers of mixed signedness are compared by value via std::cmp_equal rather than after
    //! the usual arithmetic conversions — so `checkEqual(-1, ~0u)` fails instead of passing, and a
    //! `size()` can be checked against a plain integer literal without a cast to silence the
    //! compilers' sign-comparison warnings.
    template <typename A, typename B>
    void checkEqual(const A& lhs, const B& rhs, std::string_view lhs_expr, std::string_view rhs_expr, std::source_location loc = std::source_location::current())
    {
        bool equal{false};
        if constexpr (is_comparable_integer_v<A> && is_comparable_integer_v<B>) {
            equal = std::cmp_equal(lhs, rhs);
        } else {
            equal = (lhs == rhs);
        }

        if (!equal) {
            checkEqualFailed(lhs_expr, rhs_expr, toString(lhs), toString(rhs), loc);
        }
    }

    //! Checks that two floating-point values differ by less than `tolerance`; reports failure with
    //! both values, their difference and the tolerance, all at full precision.
    //!
    //! A NaN anywhere fails, which is why the test is written `!(difference < tolerance)` rather
    //! than `difference >= tolerance`: a comparison that cannot be evaluated is a failing one.
    template <typename A, typename B, typename Tol>
        requires(std::is_floating_point_v<A> && std::is_floating_point_v<B> && std::is_floating_point_v<Tol>)
    void checkClose(A lhs, B rhs, Tol tolerance, std::string_view lhs_expr, std::string_view rhs_expr, std::source_location loc = std::source_location::current())
    {
        using Common = std::common_type_t<A, B, Tol>;
        const Common difference{std::abs(static_cast<Common>(lhs) - static_cast<Common>(rhs))};
        if (!(difference < static_cast<Common>(tolerance))) {
            checkCloseFailed(lhs_expr, rhs_expr, toString(lhs), toString(rhs), toString(difference), toString(tolerance), loc);
        }
    }

    //! Checks that a callable throws any exception.
    template <typename Fn> void checkThrows(Fn&& fn, std::string_view expr, std::source_location loc = std::source_location::current())
    {
        bool threw = false;
        try {
            fn();
        } catch (...) {
            threw = true;
        }
        if (!threw) {
            std::string msg(expr);
            msg += " did not throw";
            checkFailed(msg, loc);
        }
    }

} // namespace TestingLib

// ---------------------------------------------------------------------------
// Thin macros — only used for expression stringification (#expr)
// ---------------------------------------------------------------------------

// MSVC C4127: "conditional expression is constant" on do { } while(false) — standard macro pattern.
#ifdef _MSC_VER
#define TEST_CHECK_SUPPRESS_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4127))
#define TEST_CHECK_SUPPRESS_END __pragma(warning(pop))
#else
#define TEST_CHECK_SUPPRESS_BEGIN
#define TEST_CHECK_SUPPRESS_END
#endif

//! Fails with file, line, and stringified expression if `expr` is false.
#define TEST_CHECK(expr)                      \
    TEST_CHECK_SUPPRESS_BEGIN                 \
    do {                                      \
        if (!(expr)) {                        \
            ::TestingLib::checkFailed(#expr); \
        }                                     \
    } while (false) TEST_CHECK_SUPPRESS_END

//! Fails showing both values if `a != b`.
#define TEST_CHECK_EQUAL(a, b) ::TestingLib::checkEqual((a), (b), #a, #b)

//! Fails showing both values and their difference if `a` and `b` differ by `tolerance` or more.
#define TEST_CHECK_CLOSE(a, b, tolerance) ::TestingLib::checkClose((a), (b), (tolerance), #a, #b)

//! Fails if `expr` does not throw.
#define TEST_CHECK_THROWS(expr) \
    ::TestingLib::checkThrows(  \
        [&] {                   \
            (void)(expr);       \
        },                      \
        #expr)

//! Defines and auto-registers a test case.
#define TEST_CASE(test_name)                                       \
    static void test_name();                                       \
    namespace                                                      \
    {                                                              \
        struct test_name##_registrar {                             \
            test_name##_registrar()                                \
            {                                                      \
                ::TestingLib::registerTest(#test_name, test_name); \
            }                                                      \
        } test_name##_instance;                                    \
    }                                                              \
    static void test_name()
