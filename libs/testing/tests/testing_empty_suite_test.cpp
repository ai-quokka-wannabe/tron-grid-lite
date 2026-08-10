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
    Deliberately registers no test case, and passes only if runAll() reports failure.

    This is the floor below every other suite in the repository: test cases register themselves
    through static registrar objects, and if that mechanism ever silently broke — a linker
    discarding unreferenced translation units is the classic way — a suite would report the same
    "0 failed" as a healthy one, green forever over nothing. A runner with nothing to run must say
    so as a failure, and this binary is what notices if it stops doing that.

    It cannot live inside testing_self_tests: any TEST_CASE written to check the empty-suite floor
    makes the suite non-empty.
*/

#include "testing/testing.hpp"

int main()
{
    return TestingLib::runAll() ? 0 : 1;
}
