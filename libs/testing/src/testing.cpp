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
#include <iostream>
#include <stdexcept>

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace TestingLib
{

    namespace
    {

        /*!
            Sends failed C runtime assertions to stderr instead of to a message box.

            On Windows, a Debug build's CRT reports a failed assertion — including the debug STL's
            own bounds checks on `std::vector::operator[]` — by opening a **modal dialog** and
            waiting for somebody to click it. In an interactive session that is merely rude. In CI
            it is a hang: the job has nobody to click the box, so a test that would have failed in
            milliseconds instead sits there until the runner's timeout kills it, and the log says
            nothing about which test it was.

            Redirecting the report turns that into an ordinary crash with a message on stderr, which
            ctest records as a failure with the assertion text attached.

            Guarded on `_DEBUG` as well as `_WIN32`, and not merely for tidiness: outside a debug CRT
            these calls are macros that expand to nothing, which leaves the loop variable unread and
            fails the build under `/WX`. There is nothing to redirect there anyway — a release build
            defines no assertions — and nothing to do on Linux, whose C runtime has no such behaviour
            to correct.
        */
        void silenceAssertionDialogs()
        {
#if defined(_WIN32) && defined(_DEBUG)
            for (const int report_type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
                _CrtSetReportMode(report_type, _CRTDBG_MODE_FILE);
                _CrtSetReportFile(report_type, _CRTDBG_FILE_STDERR);
            }
#endif
        }

    } // namespace

    //! Internal exception thrown when a test check fails.
    struct CheckFailure : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> cases;
        return cases;
    }

    void registerTest(std::string_view name, std::function<void()> fn)
    {
        registry().push_back({name, std::move(fn)});
    }

    bool runAll()
    {
        silenceAssertionDialogs();

        const std::vector<TestCase>& cases{registry()};

        /*
            The floor below every other check: test cases register themselves through static
            registrar objects, so a suite whose registration has silently broken — a linker
            discarding unreferenced translation units is the classic way — reports the same
            "0 failed" as a healthy one. Nothing to run is a failure, not a pass.
        */
        if (cases.empty()) {
            std::cout << "  FAIL  no tests are registered\n";
            std::cout << "\n0 passed, 0 failed, 0 total\n";
            return true;
        }

        std::size_t passed{0};
        std::size_t failed{0};

        for (const TestCase& tc : cases) {
            try {
                tc.fn();
                std::cout << "  PASS  " << tc.name << "\n";
                ++passed;
            } catch (const CheckFailure& e) {
                std::cout << "  FAIL  " << tc.name << "\n";
                std::cout << "        " << e.what() << "\n";
                ++failed;
            } catch (const std::exception& e) {
                std::cout << "  FAIL  " << tc.name << " (unhandled exception)\n";
                std::cout << "        " << e.what() << "\n";
                ++failed;
            } catch (...) {
                // Without this, anything that is not a std::exception escapes runAll, propagates
                // out of main and reaches std::terminate — remaining tests skipped, no summary,
                // and an abnormal exit in place of a meaningful one.
                std::cout << "  FAIL  " << tc.name << " (unknown exception)\n";
                ++failed;
            }
        }

        std::cout << "\n" << passed << " passed, " << failed << " failed, " << cases.size() << " total\n";
        return failed > 0;
    }

    [[noreturn]] void checkFailed(std::string_view expr, std::source_location loc)
    {
        std::string msg;
        msg += loc.file_name();
        msg += ":";
        msg += std::to_string(loc.line());
        msg += ": check failed: ";
        msg += expr;
        throw CheckFailure(msg);
    }

    [[noreturn]] void checkEqualFailed(std::string_view lhs_expr, std::string_view rhs_expr, std::string_view lhs_val, std::string_view rhs_val, std::source_location loc)
    {
        std::string msg;
        msg += loc.file_name();
        msg += ":";
        msg += std::to_string(loc.line());
        msg += ": check equal failed: ";
        msg += lhs_expr;
        msg += " == ";
        msg += rhs_expr;
        msg += " (";
        msg += lhs_val;
        msg += " != ";
        msg += rhs_val;
        msg += ")";
        throw CheckFailure(msg);
    }

    [[noreturn]] void checkCloseFailed(std::string_view lhs_expr, std::string_view rhs_expr, std::string_view lhs_val, std::string_view rhs_val,
        std::string_view diff_val, std::string_view tolerance_val, std::source_location loc)
    {
        std::string msg;
        msg += loc.file_name();
        msg += ":";
        msg += std::to_string(loc.line());
        msg += ": check close failed: ";
        msg += lhs_expr;
        msg += " ~ ";
        msg += rhs_expr;
        msg += " (";
        msg += lhs_val;
        msg += " vs ";
        msg += rhs_val;
        msg += ", difference ";
        msg += diff_val;
        msg += " not below tolerance ";
        msg += tolerance_val;
        msg += ")";
        throw CheckFailure(msg);
    }

} // namespace TestingLib
