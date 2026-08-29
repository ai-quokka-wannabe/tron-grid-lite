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
    \file abi_manifest.c
    Writes the Program ABI's memory layout out as a table any language can read.

    The header pins its layout with static assertions, and that protects exactly two audiences: a
    Program written in C or C++, and one written in a language that can consume a C header directly.
    Everybody else — Go, D, Nim, Odin, Ada, Fortran, Swift, Haskell, OCaml, or a hand-written binding
    in anything — declares the structs a second time and gets no check at all. A single wrong offset
    then compiles perfectly and arrives as nonsense in a sensor buffer, which is the worst shape a
    defect can take here: it looks like the creature is confused rather than like the binding is
    broken, and it is the Program's author who loses the week.

    So the layout is published as data. A binding in any language can assert against this file in its
    own test suite and get back precisely what the static assertions give C.

    Every number here comes from the compiler rather than from arithmetic, because a hand-computed
    manifest would be one more copy of the fact it exists to protect.
*/

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <tgl/tgl_program_abi.h>

#if defined(__cplusplus)
#define TGL_ALIGNOF(type) alignof(type)
#else
#define TGL_ALIGNOF(type) _Alignof(type)
#endif

/*
    Members, once, in declaration order. The emitter and the self-check below both walk these lists,
    so a member that is added to the header and forgotten here cannot silently vanish from the
    manifest: the sizes would no longer add up to the struct, and the self-check says so.
*/

/* clang-format off */
#define TGL_LIBRARY_INFO_MEMBERS(X) \
    X(TglLibraryInfo, creature_count) \
    X(TglLibraryInfo, nominal_dt_seconds)

#define TGL_EYE_DESC_MEMBERS(X) \
    X(TglEyeDesc, sample_directions) \
    X(TglEyeDesc, sample_acceptance_angles) \
    X(TglEyeDesc, position) \
    X(TglEyeDesc, sample_count) \
    X(TglEyeDesc, channels) \
    X(TglEyeDesc, quantisation_bits)

#define TGL_EAR_DESC_MEMBERS(X) \
    X(TglEarDesc, band_edges_hz) \
    X(TglEarDesc, air_absorption_db_per_km) \
    X(TglEarDesc, position) \
    X(TglEarDesc, band_count) \
    X(TglEarDesc, bin_count) \
    X(TglEarDesc, bin_seconds)

#define TGL_CREATURE_DESC_MEMBERS(X) \
    X(TglCreatureDesc, creature_id) \
    X(TglCreatureDesc, random_seed) \
    X(TglCreatureDesc, eyes) \
    X(TglCreatureDesc, ears) \
    X(TglCreatureDesc, eye_count) \
    X(TglCreatureDesc, ear_count) \
    X(TglCreatureDesc, irradiance_sample_count) \
    X(TglCreatureDesc, max_contact_count) \
    X(TglCreatureDesc, max_forward_speed) \
    X(TglCreatureDesc, max_turn_rate) \
    X(TglCreatureDesc, max_vocalisation_strength) \
    X(TglCreatureDesc, max_joint_angle) \
    X(TglCreatureDesc, max_joint_torque) \
    X(TglCreatureDesc, padding0)

#define TGL_RENDER_MATERIAL_MEMBERS(X) \
    X(TglRenderMaterial, colour) \
    X(TglRenderMaterial, index_of_refraction) \
    X(TglRenderMaterial, emission) \
    X(TglRenderMaterial, transmission)

#define TGL_RENDER_TRIANGLE_MEMBERS(X) \
    X(TglRenderTriangle, vertices) \
    X(TglRenderTriangle, material)

#define TGL_RENDER_MODEL_MEMBERS(X) \
    X(TglRenderModel, vertex_positions) \
    X(TglRenderModel, triangles) \
    X(TglRenderModel, materials) \
    X(TglRenderModel, vertex_count) \
    X(TglRenderModel, triangle_count) \
    X(TglRenderModel, material_count) \
    X(TglRenderModel, segment_count) \
    X(TglRenderModel, segment_spacing) \
    X(TglRenderModel, padding0)

#define TGL_EYE_VIEW_MEMBERS(X) \
    X(TglEyeView, samples) \
    X(TglEyeView, sample_count) \
    X(TglEyeView, channels)

#define TGL_EAR_VIEW_MEMBERS(X) \
    X(TglEarView, energy) \
    X(TglEarView, arrivals) \
    X(TglEarView, arrival_count) \
    X(TglEarView, band_count) \
    X(TglEarView, bin_count) \
    X(TglEarView, reserved0)

#define TGL_ARRIVAL_MEMBERS(X) \
    X(TglArrival, onset_seconds) \
    X(TglArrival, radial_velocity) \
    X(TglArrival, energy)

#define TGL_CONTACT_MEMBERS(X) \
    X(TglContact, position) \
    X(TglContact, impulse) \
    X(TglContact, normal) \
    X(TglContact, depth) \
    X(TglContact, slip)

#define TGL_SENSES_MEMBERS(X) \
    X(TglSenses, tick) \
    X(TglSenses, eyes) \
    X(TglSenses, ears) \
    X(TglSenses, contacts) \
    X(TglSenses, eye_count) \
    X(TglSenses, ear_count) \
    X(TglSenses, contact_count) \
    X(TglSenses, dt_seconds) \
    X(TglSenses, body_forward_speed) \
    X(TglSenses, body_vertical_speed) \
    X(TglSenses, body_turn_rate) \
    X(TglSenses, specific_force) \
    X(TglSenses, angular_velocity) \
    X(TglSenses, irradiance)

#define TGL_ACTIONS_MEMBERS(X) \
    X(TglActions, desired_forward_speed) \
    X(TglActions, desired_turn_rate) \
    X(TglActions, vocalisation_strength) \
    X(TglActions, joint_targets)

#define TGL_PROGRAM_VTABLE_MEMBERS(X) \
    X(TglProgramVTable, struct_size) \
    X(TglProgramVTable, abi_version) \
    X(TglProgramVTable, library_init) \
    X(TglProgramVTable, program_rez) \
    X(TglProgramVTable, program_tick) \
    X(TglProgramVTable, program_derez) \
    X(TglProgramVTable, library_shutdown)

#define TGL_EACH_STRUCT(S) \
    S(TglLibraryInfo, TGL_LIBRARY_INFO_MEMBERS) \
    S(TglEyeDesc, TGL_EYE_DESC_MEMBERS) \
    S(TglEarDesc, TGL_EAR_DESC_MEMBERS) \
    S(TglCreatureDesc, TGL_CREATURE_DESC_MEMBERS) \
    S(TglRenderMaterial, TGL_RENDER_MATERIAL_MEMBERS) \
    S(TglRenderTriangle, TGL_RENDER_TRIANGLE_MEMBERS) \
    S(TglRenderModel, TGL_RENDER_MODEL_MEMBERS) \
    S(TglEyeView, TGL_EYE_VIEW_MEMBERS) \
    S(TglEarView, TGL_EAR_VIEW_MEMBERS) \
    S(TglContact, TGL_CONTACT_MEMBERS) \
    S(TglArrival, TGL_ARRIVAL_MEMBERS) \
    S(TglSenses, TGL_SENSES_MEMBERS) \
    S(TglActions, TGL_ACTIONS_MEMBERS) \
    S(TglProgramVTable, TGL_PROGRAM_VTABLE_MEMBERS)
/* clang-format on */

enum {
    MANIFEST_CAPACITY = 65536
};

static char g_manifest[MANIFEST_CAPACITY];
static size_t g_length = 0u;
static int g_overflowed = 0;

static void emit(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);

    const size_t remaining = MANIFEST_CAPACITY - g_length;
    const int written = vsnprintf(g_manifest + g_length, remaining, format, arguments);

    va_end(arguments);

    if ((written < 0) || ((size_t)written >= remaining)) {
        g_overflowed = 1;
        return;
    }

    g_length += (size_t)written;
}

#define EMIT_MEMBER(type, member) emit("member %s %s %u %u\n", #type, #member, (unsigned)offsetof(type, member), (unsigned)TGL_SIZEOF_MEMBER(type, member));

#define SUM_MEMBER(type, member) +(unsigned)TGL_SIZEOF_MEMBER(type, member)

#define EMIT_STRUCT(type, MEMBERS)                                                           \
    emit("\nstruct %s %u %u\n", #type, (unsigned)sizeof(type), (unsigned)TGL_ALIGNOF(type)); \
    MEMBERS(EMIT_MEMBER)

/*
    Every struct in this ABI is free of padding, which the header asserts. So the members listed
    above must account for every byte of the struct they belong to, and if one has been added to the
    header but not to the list here the totals disagree. Without this, forgetting a member would
    publish a manifest that is quietly incomplete — and a binding generated from it would be wrong in
    exactly the way the manifest exists to prevent.
*/
#define CHECK_STRUCT(type, MEMBERS)                                                                                                                                  \
    if ((unsigned)(0u MEMBERS(SUM_MEMBER)) != (unsigned)sizeof(type)) {                                                                                              \
        fprintf(stderr, "abi_manifest: the members listed for %s add up to %u bytes but the struct is %u. A member was added to the header and not to this file.\n", \
            #type, (unsigned)(0u MEMBERS(SUM_MEMBER)), (unsigned)sizeof(type));                                                                                      \
        failures++;                                                                                                                                                  \
    }

static int build(void)
{
    int failures = 0;
    TGL_EACH_STRUCT(CHECK_STRUCT)
    if (failures != 0) {
        return 0;
    }

    emit("# TronGrid Lite Program ABI memory layout.\n");
    emit("#\n");
    emit("# Generated from the compiler by libs/program-abi/tools/abi_manifest.c. Do not edit: run\n");
    emit("#   cmake --build <dir> --target tgl_abi_manifest_write\n");
    emit("# and commit the result. A test compares this file against the layout on every build.\n");
    emit("#\n");
    emit("# Published so that a Program written in a language which cannot consume a C header can\n");
    emit("# still check its own struct definitions, and get what the header's static assertions give\n");
    emit("# a Program written in C.\n");
    emit("#\n");
    emit("#   abi-version <n>                     the layout below belongs to this ABI version\n");
    emit("#   pointer-size <bytes>                every pointer in the interface\n");
    emit("#   struct <name> <size> <alignment>    all sizes and offsets in bytes\n");
    emit("#   member <struct> <name> <offset> <size>\n");
    emit("#\n");
    emit("# No struct here contains padding, so a struct's members always account for all of it.\n");
    emit("\n");
    emit("abi-version %u\n", (unsigned)TGL_ABI_VERSION);
    emit("pointer-size %u\n", (unsigned)sizeof(void*));

    TGL_EACH_STRUCT(EMIT_STRUCT)

    return g_overflowed == 0;
}

/*! Reads a file, dropping carriage returns so that a working copy checked out on Windows compares
    equal to one checked out on Linux. */
static size_t readNormalised(const char* path, char* buffer, size_t capacity)
{
    FILE* const file = fopen(path, "rb");
    if (file == NULL) {
        return (size_t)-1;
    }

    const size_t bytes_read = fread(buffer, 1u, capacity, file);
    fclose(file);

    size_t length = 0u;
    for (size_t index = 0u; index < bytes_read; ++index) {
        if (buffer[index] != '\r') {
            buffer[length++] = buffer[index];
        }
    }

    return length;
}

static int checkManifest(const char* path)
{
    static char expected[MANIFEST_CAPACITY];
    const size_t length = readNormalised(path, expected, MANIFEST_CAPACITY);

    if (length == (size_t)-1) {
        fprintf(stderr, "abi_manifest: cannot read %s\n", path);
        return 1;
    }

    if ((length == g_length) && (memcmp(expected, g_manifest, length) == 0)) {
        printf("The ABI layout manifest matches the header.\n");
        return 0;
    }

    /* The first differing line is what somebody needs; the whole file is what they do not. */
    size_t line = 1u;
    size_t index = 0u;
    while ((index < length) && (index < g_length) && (expected[index] == g_manifest[index])) {
        if (g_manifest[index] == '\n') {
            line++;
        }
        index++;
    }

    fprintf(stderr,
        "abi_manifest: %s no longer describes the ABI, from line %u.\n"
        "The layout changed. Regenerate it with the tgl_abi_manifest_write target and commit the\n"
        "result, and make sure TGL_ABI_VERSION was bumped if a Program could notice the difference.\n",
        path, (unsigned)line);
    return 1;
}

static int writeManifest(const char* path)
{
    /* Binary, so that the file is LF on every platform and two checkouts agree. */
    FILE* const file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "abi_manifest: cannot write %s\n", path);
        return 1;
    }

    const size_t written = fwrite(g_manifest, 1u, g_length, file);
    const int closed = fclose(file);

    if ((written != g_length) || (closed != 0)) {
        fprintf(stderr, "abi_manifest: failed to write %s\n", path);
        return 1;
    }

    printf("Wrote %s.\n", path);
    return 0;
}

int main(int argc, char** argv)
{
    if (!build()) {
        return 1;
    }

    if ((argc == 3) && (strcmp(argv[1], "--check") == 0)) {
        return checkManifest(argv[2]);
    }

    if ((argc == 3) && (strcmp(argv[1], "--write") == 0)) {
        return writeManifest(argv[2]);
    }

    if (argc == 1) {
        fwrite(g_manifest, 1u, g_length, stdout);
        return 0;
    }

    fprintf(stderr, "usage: abi_manifest [--check <path> | --write <path>]\n");
    return 2;
}
