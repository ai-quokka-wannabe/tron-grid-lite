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

/*!
    \file program_abi_tests.cpp
    What is left to check once the header has been compiled.

    There are few cases here and that is the right number. Every layout fact the ABI depends on is a
    static assertion inside the header, so it is checked by the act of compiling this file — and,
    because the same assertions compile in `abi_c99_compile.c` and `test_program.c` as C, the two
    languages are already pinned against each other. A runtime re-check of the offsets would only
    restate what the build has already proved, and would go green in exactly the cases the
    assertions go red.

    What compilation cannot check is a macro that expands to the wrong value, because a wrong value
    is still a valid expression. That is what remains.
*/

#include <tgl/tgl_program_abi.h>

#include <testing/testing.hpp>

namespace
{
    // A C++ Program, where the C fixture next door is a C one. TGL_NOEXCEPT is `noexcept` here and
    // part of each pointer's type, so these definitions failing to compile would itself be the
    // finding.
    void libraryInit(const TglLibraryInfo*) noexcept
    {
    }

    TglProgram* programRez(const TglCreatureDesc*, TglRenderModel*) noexcept
    {
        return nullptr;
    }

    void programTick(TglProgram*, const TglSenses*, TglActions*) noexcept
    {
    }

    void programDerez(TglProgram*) noexcept
    {
    }

    void libraryShutdown() noexcept
    {
    }

    const TglProgramVTable g_vtable = {TGL_PROGRAM_VTABLE_HEADER, libraryInit, programRez, programTick, programDerez, libraryShutdown};
}

//! The two header members are filled by a macro, so nothing about writing them is checked by the
//! compiler. A vtable claiming the wrong size is precisely the failure struct_size exists to
//! prevent, and it would be indistinguishable from a correct one until the Grid read off the end.
TEST_CASE(the_vtable_header_macro_states_the_real_size_and_version)
{
    TEST_CHECK_EQUAL(g_vtable.struct_size, static_cast<uint32_t>(sizeof(TglProgramVTable)));
    TEST_CHECK_EQUAL(g_vtable.abi_version, static_cast<uint32_t>(TGL_ABI_VERSION));
}

//! The floor the loader will compare against has to be reachable by a vtable that is correct today,
//! and must not be so low that a vtable missing an entry point clears it.
TEST_CASE(the_minimum_vtable_size_admits_this_vtable_and_nothing_smaller)
{
    TEST_CHECK(g_vtable.struct_size >= TGL_PROGRAM_VTABLE_MIN_SIZE);
    TEST_CHECK(TGL_PROGRAM_VTABLE_MIN_SIZE > offsetof(TglProgramVTable, library_shutdown));
}

//! Every entry point is documented "must not be NULL", which is a rule about what the loader will
//! refuse rather than something the type system enforces.
TEST_CASE(a_conforming_vtable_leaves_no_entry_point_null)
{
    TEST_CHECK(g_vtable.library_init != nullptr);
    TEST_CHECK(g_vtable.program_rez != nullptr);
    TEST_CHECK(g_vtable.program_tick != nullptr);
    TEST_CHECK(g_vtable.program_derez != nullptr);
    TEST_CHECK(g_vtable.library_shutdown != nullptr);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
