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
#include <stdlib.h>

#include <tgl/tgl_program_abi.h>

static int g_creature_state = 0;

#if defined(TGL_DRIVER_REFUSES_SECOND_REZ)
/* How many creatures this Program wears right now. The variant takes the first and refuses the
   second, and holds the Grid to the header's word in the one place a fixture can: a shutdown
   with a creature still rezzed is the contract broken, and aborts the process rather than
   letting the test pass by not looking. */
static int g_rezzed = 0;
#endif

#if defined(TGL_DRIVER_MODELLED) || defined(TGL_DRIVER_MISSHAPEN) || defined(TGL_DRIVER_CHAINED)
/* A little pyramid of a body: four vertices, four faces, a near-black mirror hull and one glowing
   tail face. Small enough that a test can check every number by hand, and real enough to exercise
   every array a model carries. The nose sits at the head sensors' station. */
static const float g_model_vertices[] = {
    0.0f,
    0.05f,
    -0.2f, /* nose */
    -0.1f,
    -0.05f,
    0.2f, /* tail port */
    0.1f,
    -0.05f,
    0.2f, /* tail starboard */
    0.0f,
    0.1f,
    0.2f, /* tail top */
};

static const TglRenderMaterial g_model_materials[] = {
    {{0.02f, 0.02f, 0.03f}, 1.5f, {0.0f, 0.0f, 0.0f}, 0.0f}, /* the hull: a mirror, almost black */
    {{0.05f, 0.05f, 0.05f}, 1.5f, {0.1f, 2.0f, 3.0f}, 0.0f}, /* the tail: neon */
};
#endif

#if defined(TGL_DRIVER_MODELLED) || defined(TGL_DRIVER_CHAINED)
static const TglRenderTriangle g_model_triangles[] = {
    {{0u, 2u, 1u}, 0u}, /* belly */
    {{0u, 1u, 3u}, 0u}, /* port flank */
    {{0u, 3u, 2u}, 0u}, /* starboard flank */
    {{1u, 2u, 3u}, 1u}, /* tail, glowing */
};
#elif defined(TGL_DRIVER_MISSHAPEN)
/* The same body except that the port flank names a vertex that does not exist. The Grid must
   refuse the whole rez rather than keep the salvageable part: a model is accepted entire or not
   at all. */
static const TglRenderTriangle g_model_triangles[] = {
    {{0u, 2u, 1u}, 0u},
    {{0u, 9u, 3u}, 0u}, /* vertex 9 of a model with four */
    {{0u, 3u, 2u}, 0u},
    {{1u, 2u, 3u}, 1u},
};
#endif

static void libraryInit(const TglLibraryInfo* info) TGL_NOEXCEPT
{
    (void)info;
}

static TglProgram* programRez(const TglCreatureDesc* desc, TglRenderModel* model) TGL_NOEXCEPT
{
    (void)desc;
    (void)model;

#if defined(TGL_DRIVER_REFUSES_SECOND_REZ)
    if (g_rezzed >= 1) {
        return NULL;
    }
    g_rezzed += 1;
    return (TglProgram*)&g_creature_state;
#elif defined(TGL_DRIVER_REFUSES_REZ)
    /* A Program that cannot take this body. Nothing is wrong with the library — the ABI says a rez
       may fail, and the Grid has to treat a null handle as a refusal rather than as a creature.

       The state is referenced anyway so that this variant does not carry an object nothing mentions,
       which Clang reports and this build treats as an error. */
    (void)&g_creature_state;
    return NULL;
#else
#if defined(TGL_DRIVER_MODELLED) || defined(TGL_DRIVER_MISSHAPEN) || defined(TGL_DRIVER_CHAINED)
    /* The Program's own storage, borrowed for this call exactly as the descriptor's arrays are
       borrowed in the other direction. The Grid copies what it accepts before the call returns. */
    model->vertex_positions = g_model_vertices;
    model->triangles = g_model_triangles;
    model->materials = g_model_materials;
    model->vertex_count = 4u;
    model->triangle_count = 4u;
    model->material_count = 2u;
    model->segment_count = 1u;
    model->segment_spacing = 0.0f;
#endif
#if defined(TGL_DRIVER_CHAINED)
    /* The same pyramid four times over: a chain of four, three tenths of a metre apart. */
    model->segment_count = 4u;
    model->segment_spacing = 0.3f;
#endif
    return (TglProgram*)&g_creature_state;
#endif
}

static void programTick(TglProgram* program, const TglSenses* senses, TglActions* actions) TGL_NOEXCEPT
{
    (void)program;
    (void)senses;

#if defined(TGL_DRIVER_EXCESSIVE)
    /* Everything a body will refuse, in one call. Ten metres per second is beyond this body's reach;
       a NaN turn rate is the one that matters, because an unsanitised NaN becomes a NaN yaw and then
       a position that poisons everything after it; and a negative loudness is not a quieter sound
       but a meaningless one. */
    actions->desired_forward_speed = 10.0f;
    actions->desired_turn_rate = nanf("");
    actions->vocalisation_strength = -5.0f;
#elif defined(TGL_DRIVER_TURNING)
    /* Walks and turns at once, which is what exercises the arc: a body doing both traces a circle,
       and the physics step owes it the exact one. Half speed, an eighth of a turn per second. */
    actions->desired_forward_speed = 0.5f;
    actions->desired_turn_rate = 0.78539816f;
#elif defined(TGL_DRIVER_CALLING)
    /* Stands still and calls at three quarters strength every tick — a train of calls, which is
       what an echolocating animal does and the only thing the Grid's one-shot vocalisation can
       produce. Stationary so a test observes the staging delay in the voice alone: the first
       tick acts on the zeroed default, and the first call sounds on the second. */
    actions->vocalisation_strength = 0.75f;
#elif defined(TGL_DRIVER_SILENT)
    /* Writes nothing at all, which is legitimate: the Grid zeroes the actions before every call, so
       a Program with nothing to say coasts to a stop rather than repeating itself. */
    (void)actions;
#elif defined(TGL_DRIVER_CHAINED)
    /* Walks and turns, so the chain the world places behind it bends where it turned: a little
       over half the body's limit, a quarter turn a second. */
    actions->desired_forward_speed = 0.6f;
    actions->desired_turn_rate = 0.5f;
#elif defined(TGL_DRIVER_MODELLED) || defined(TGL_DRIVER_MISSHAPEN)
    /* The body is the point; the Program stands. The misshapen variant never reaches this line,
       because its rez is refused, but the branch keeps the two variants one line apart. */
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
#if defined(TGL_DRIVER_REFUSES_SECOND_REZ)
    g_rezzed -= 1;
#endif
}

static void libraryShutdown(void) TGL_NOEXCEPT
{
#if defined(TGL_DRIVER_REFUSES_SECOND_REZ)
    if (g_rezzed != 0) {
        /* A Program is entitled to touch its state in program_derez and never after
           library_shutdown; a Grid that shuts the library with a creature still rezzed has
           broken that, and this fixture says so the only way a fixture can. */
        abort();
    }
#endif
}

static const TglProgramVTable g_vtable = {TGL_PROGRAM_VTABLE_HEADER, libraryInit, programRez, programTick, programDerez, libraryShutdown};

TGL_PROGRAM_EXPORT const TglProgramVTable* tglGetProgramVTable(uint32_t abi_version) TGL_NOEXCEPT
{
    if (abi_version != TGL_ABI_VERSION) {
        return NULL;
    }

    return &g_vtable;
}
