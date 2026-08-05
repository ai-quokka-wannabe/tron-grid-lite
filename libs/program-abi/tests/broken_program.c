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
    \file broken_program.c
    Program libraries that are wrong, each in exactly one way, for the loader's refusals to be tested
    against.

    One source compiled several times rather than several sources, because the interesting part of
    each fixture is the single line where it differs from a correct Program, and that line is far
    easier to see beside the others than in a file of its own. The variant is chosen by the build.

    Every one of these is a mistake somebody makes rather than an invention: forgetting to export the
    symbol, filling the vtable by hand instead of with the macro, returning a vtable while ignoring
    the version argument, and leaving an entry point out.

    There is a variant per entry point rather than one for "an entry point is missing", because the
    loader checks the five in a single condition and a condition that names one of them twice would
    pass a test that only ever nulls the third. Five fixtures make each clause of it answer for
    itself.
*/

#define TGL_PROGRAM_IMPLEMENTATION

#include <tgl/tgl_program_abi.h>

#if defined(TGL_BROKEN_NO_SYMBOL)

/*! A library that loads perfectly well and is not a Program at all. This is what a Program built
    without TGL_PROGRAM_IMPLEMENTATION looks like from outside on Windows, where the symbol simply
    never reaches the export table. */
TGL_PROGRAM_EXPORT uint32_t tglNotTheEntryPoint(void) TGL_NOEXCEPT;

TGL_PROGRAM_EXPORT uint32_t tglNotTheEntryPoint(void) TGL_NOEXCEPT
{
    return TGL_ABI_VERSION;
}

#else

static int g_creature_state = 0;

/*
    Deliberately not static. A variant that nulls one of these leaves its function referenced by
    nothing, and an unreferenced static function is a warning under both /W4 and -Wall — which this
    build turns into an error. External linkage says "something else might call this" and is true
    enough of a fixture, where the alternative is machinery whose only job is to keep a compiler
    quiet.
*/
void tglBrokenLibraryInit(const TglLibraryInfo* info) TGL_NOEXCEPT
{
    (void)info;
}

TglProgram* tglBrokenProgramRez(const TglCreatureDesc* desc) TGL_NOEXCEPT
{
    (void)desc;
    return (TglProgram*)&g_creature_state;
}

void tglBrokenProgramTick(TglProgram* program, const TglSenses* senses, TglActions* actions) TGL_NOEXCEPT
{
    (void)program;
    (void)senses;
    (void)actions;
}

void tglBrokenProgramDerez(TglProgram* program) TGL_NOEXCEPT
{
    (void)program;
}

void tglBrokenLibraryShutdown(void) TGL_NOEXCEPT
{
}

#if defined(TGL_BROKEN_NULL_LIBRARY_INIT)
#define TGL_SLOT_LIBRARY_INIT NULL
#else
#define TGL_SLOT_LIBRARY_INIT tglBrokenLibraryInit
#endif

#if defined(TGL_BROKEN_NULL_PROGRAM_REZ)
#define TGL_SLOT_PROGRAM_REZ NULL
#else
#define TGL_SLOT_PROGRAM_REZ tglBrokenProgramRez
#endif

#if defined(TGL_BROKEN_NULL_PROGRAM_TICK)
#define TGL_SLOT_PROGRAM_TICK NULL
#else
#define TGL_SLOT_PROGRAM_TICK tglBrokenProgramTick
#endif

#if defined(TGL_BROKEN_NULL_PROGRAM_DEREZ)
#define TGL_SLOT_PROGRAM_DEREZ NULL
#else
#define TGL_SLOT_PROGRAM_DEREZ tglBrokenProgramDerez
#endif

#if defined(TGL_BROKEN_NULL_LIBRARY_SHUTDOWN)
#define TGL_SLOT_LIBRARY_SHUTDOWN NULL
#else
#define TGL_SLOT_LIBRARY_SHUTDOWN tglBrokenLibraryShutdown
#endif

static const TglProgramVTable g_vtable = {
#if defined(TGL_BROKEN_SMALL_VTABLE)
    /* A vtable filled in by hand, whose author counted the members and got a stale answer. The Grid
       must not read past what this claims, and must not believe a claim smaller than version 1. */
    (uint32_t)16u, (uint32_t)TGL_ABI_VERSION,
#elif defined(TGL_BROKEN_WRONG_VERSION)
    /* Built against a different header. The exported function still answers, because a hand-written
       binding may ignore the version it was asked for. */
    (uint32_t)sizeof(TglProgramVTable), (uint32_t)(TGL_ABI_VERSION + 1u),
#else
    TGL_PROGRAM_VTABLE_HEADER,
#endif
    TGL_SLOT_LIBRARY_INIT, TGL_SLOT_PROGRAM_REZ, TGL_SLOT_PROGRAM_TICK, TGL_SLOT_PROGRAM_DEREZ, TGL_SLOT_LIBRARY_SHUTDOWN};

TGL_PROGRAM_EXPORT const TglProgramVTable* tglGetProgramVTable(uint32_t abi_version) TGL_NOEXCEPT
{
    (void)abi_version;

#if defined(TGL_BROKEN_REFUSES_VERSION)
    /* A Program that cannot satisfy this Grid and says so the way the ABI prescribes. Not a defect
       in the Program at all — the defect would be the Grid carrying on regardless.

       Its vtable is perfectly good and simply never handed over, which is what a real Program that
       supports an older ABI looks like. Referenced here so that the variant does not build a table
       nothing mentions, which Clang reports and this build treats as an error. */
    (void)&g_vtable;
    return NULL;
#else
    return &g_vtable;
#endif
}

#endif
