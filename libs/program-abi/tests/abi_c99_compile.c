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
    \file abi_c99_compile.c
    Compiling the ABI header under C99 is the entire test, and it is not a formality.

    Every layout assertion in the header fires here, and under C99 they take a path nothing else in
    this repository compiles: TGL_STATIC_ASSERT has no _Static_assert to reach for and falls back to
    a typedef of an array whose size goes negative when the condition fails. That fallback exists for
    Programs built by other people with older toolchains, so it is exactly the branch most likely to
    rot unnoticed and least likely to be exercised by accident.

    MSVC has no /std:c99, so there this compiles in the default C mode instead. That reaches the same
    branch for the same reason — the mode leaves __STDC_VERSION__ undefined, the #elif guarding
    _Static_assert is written with defined(), and the #else catches it. Different route, same answer.

    The header is included twice on purpose. It is meant to be vendored into other trees, where two
    copies arriving by different paths must collapse to one, and an include guard that does not work
    produces a wall of redefinition errors rather than anything subtle.
*/

#include <tgl/tgl_program_abi.h>
#include <tgl/tgl_program_abi.h>

/*! ISO C has no empty translation unit, and -Wpedantic says so. */
uint32_t tglAbiC99CompiledAgainstVersion(void);

uint32_t tglAbiC99CompiledAgainstVersion(void)
{
    return TGL_ABI_VERSION;
}
