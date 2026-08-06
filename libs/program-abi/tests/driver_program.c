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
    \file driver_program.c
    Programs that drive a body, so that the tick loop can be watched rather than assumed.

    These are correct Programs, unlike the ones in broken_program.c: what varies is what they ask a
    body to do. One asks for something reasonable, one asks for far more than any body can give, and
    one declines to rez at all. Between them the whole of the Grid's side of a tick is observable
    from outside — where the creature ended up, whether a bound bit, and what happens when a Program
    says no.

    They think about nothing and they are not meant to. Cognition belongs in another repository.
*/

#define TGL_PROGRAM_IMPLEMENTATION

#include <math.h>

#include <tgl/tgl_program_abi.h>

static int g_creature_state = 0;

static void libraryInit(const TglLibraryInfo* info) TGL_NOEXCEPT
{
    (void)info;
}

static TglProgram* programRez(const TglCreatureDesc* desc) TGL_NOEXCEPT
{
    (void)desc;

#if defined(TGL_DRIVER_REFUSES_REZ)
    /* A Program that cannot take this body. Nothing is wrong with the library — the ABI says a rez
       may fail, and the Grid has to treat a null handle as a refusal rather than as a creature.

       The state is referenced anyway so that this variant does not carry an object nothing mentions,
       which Clang reports and this build treats as an error. */
    (void)&g_creature_state;
    return NULL;
#else
    return (TglProgram*)&g_creature_state;
#endif
}

static void programTick(TglProgram* program, const TglSenses* senses, TglActions* actions) TGL_NOEXCEPT
{
    (void)program;
    (void)senses;

#if defined(TGL_DRIVER_EXCESSIVE)
    /* Everything a body will refuse, in one call. Half a metre per second is within reach and ten is
       not; a hundred radians a second is not; a vertical speed this body cannot produce at all; a
       negative loudness, which is not a quieter sound but a meaningless one; and a NaN, which is the
       one that matters because an unsanitised NaN becomes a NaN position and then a traversal that
       does not terminate. */
    actions->desired_forward_speed = 10.0f;
    actions->desired_turn_rate = 100.0f;
    actions->desired_vertical_speed = nanf("");
    actions->vocalisation_strength = -5.0f;
#elif defined(TGL_DRIVER_SILENT)
    /* Writes nothing at all, which is legitimate: the Grid zeroes the actions before every call, so
       a Program with nothing to say coasts to a stop rather than repeating itself. */
    (void)actions;
#else
    /* Straight ahead at half the body's limit, turning not at all. Chosen so the arithmetic is exact
       in binary32 and a test can assert an exact position: 0.5 m/s at 0.03125 s a tick is 0.015625 m
       a tick, and thirty-two of those is half a metre to the last bit. */
    actions->desired_forward_speed = 0.5f;
    actions->desired_turn_rate = 0.0f;
#endif
}

static void programDerez(TglProgram* program) TGL_NOEXCEPT
{
    (void)program;
}

static void libraryShutdown(void) TGL_NOEXCEPT
{
}

static const TglProgramVTable g_vtable = {TGL_PROGRAM_VTABLE_HEADER, libraryInit, programRez, programTick, programDerez, libraryShutdown};

TGL_PROGRAM_EXPORT const TglProgramVTable* tglGetProgramVTable(uint32_t abi_version) TGL_NOEXCEPT
{
    if (abi_version != TGL_ABI_VERSION) {
        return NULL;
    }

    return &g_vtable;
}
