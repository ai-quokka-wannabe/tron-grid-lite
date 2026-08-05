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
    \file test_program.c
    The smallest Program that satisfies the ABI, built as a shared library from plain C11.

    It is a fixture rather than a demonstration: it drives nothing, and a creature it rezzes onto
    stands still for ever. What it is for is that building it exercises the half of the header the
    Grid's own build never touches — TGL_PROGRAM_IMPLEMENTATION, TGL_PROGRAM_EXPORT, the exported
    symbol name, and the five function-pointer types accepting real functions written in C. If this
    library links, a Program can be written against the header.

    Deliberately not the demo Program. That one belongs to another repository, has a GUI, and is
    driven by a human; this one exists so a test can load something whose every answer is known.
*/

#define TGL_PROGRAM_IMPLEMENTATION

#include <tgl/tgl_program_abi.h>

/*! The handle handed back at rez. Its address is the only thing that matters — the Grid never
    dereferences it — so one static object serves every creature. */
static int g_creature_state = 0;

/*! Which lifecycle entry points have been called, as bit flags.

    Scaffolding, and the only thing here that a real Program would not have. It exists because "the
    loader calls library_init once the library is accepted, and library_shutdown before unloading"
    is otherwise a claim with nothing behind it: a loader that skipped both would pass every other
    test in the suite, since nothing else can tell whether a no-op was called or not. */
static uint32_t g_lifecycle = 0u;

#define TGL_TEST_LIFECYCLE_INIT 1u
#define TGL_TEST_LIFECYCLE_SHUTDOWN 2u

/*! Reads the flags above. Exported so a test can hold its own reference to this library, watch the
    Grid load and unload it, and see what actually happened in between. */
TGL_PROGRAM_EXPORT uint32_t tglTestProgramLifecycle(void) TGL_NOEXCEPT;

TGL_PROGRAM_EXPORT uint32_t tglTestProgramLifecycle(void) TGL_NOEXCEPT
{
    return g_lifecycle;
}

static void libraryInit(const TglLibraryInfo* info) TGL_NOEXCEPT
{
    (void)info;
    g_lifecycle |= TGL_TEST_LIFECYCLE_INIT;
}

static TglProgram* programRez(const TglCreatureDesc* desc) TGL_NOEXCEPT
{
    (void)desc;
    return (TglProgram*)&g_creature_state;
}

static void programTick(TglProgram* program, const TglSenses* senses, TglActions* actions) TGL_NOEXCEPT
{
    (void)program;
    (void)senses;
    (void)actions;
}

static void programDerez(TglProgram* program) TGL_NOEXCEPT
{
    (void)program;
}

static void libraryShutdown(void) TGL_NOEXCEPT
{
    g_lifecycle |= TGL_TEST_LIFECYCLE_SHUTDOWN;
}

/*! Static storage duration, as the header requires: the Grid holds this pointer past
    library_shutdown and never frees it. */
static const TglProgramVTable g_vtable = {TGL_PROGRAM_VTABLE_HEADER, libraryInit, programRez, programTick, programDerez, libraryShutdown};

TGL_PROGRAM_EXPORT const TglProgramVTable* tglGetProgramVTable(uint32_t abi_version) TGL_NOEXCEPT
{
    /* No negotiation and no shims: a Program that cannot satisfy the Grid's version says so by
       returning NULL, and the Grid refuses the run. */
    if (abi_version != TGL_ABI_VERSION) {
        return NULL;
    }

    return &g_vtable;
}
