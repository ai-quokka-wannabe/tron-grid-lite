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
    \file tgl_program_abi.h
    The whole contract between the Grid and a Program. One self-contained C header depending on
    nothing but <stdint.h> and <stddef.h>, and on no Grid internal whatsoever.

    An include guard rather than the repository's usual `#pragma once`, because this file is meant
    to be vendored into other trees and two copies reaching one translation unit by different paths
    must collapse to one. That is the only deliberate style deviation in the file.
*/

#ifndef TGL_PROGRAM_ABI_H
#define TGL_PROGRAM_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ================================================================================================
   Version
   ================================================================================================ */

/*!
    Bumped whenever anything in this file changes that an already-built Program could notice: a
    member added, removed or reordered anywhere, a signature changed, a unit or sign convention
    corrected, a threading promise widened or narrowed.

    Exactly one change does not bump it, because it cannot hurt an already-built Program: appending
    a member to the end of TglProgramVTable, which the Grid reaches only after checking struct_size.

    `tools/check_abi_version.py` fingerprints this file with this line removed, and with comments and
    whitespace removed so that rewording a doc comment or reflowing a declaration costs nothing. CI
    runs it on every push and fails the build when the fingerprint moves and this number does not.
    The rule is therefore a mechanism rather than a discipline, which matters: a forgotten bump
    produces exactly the silent memory corruption the number exists to prevent, and produces it
    without failing anywhere.
*/
#define TGL_ABI_VERSION 10u

/* ================================================================================================
   Toolchain glue
   ================================================================================================ */

/*! Applied by a Program to its one exported symbol. A Program defines TGL_PROGRAM_IMPLEMENTATION
    before including this file; the Grid, which includes it only for the layouts, does not. */
#if defined(TGL_PROGRAM_IMPLEMENTATION)
#if defined(_WIN32)
#define TGL_PROGRAM_EXPORT __declspec(dllexport)
#else
#define TGL_PROGRAM_EXPORT __attribute__((visibility("default")))
#endif
#else
#define TGL_PROGRAM_EXPORT
#endif

/*!
    No exception may cross this boundary, in either direction.

    Since C++17 `noexcept` is part of a function pointer's type, so on the C++ side this macro
    enforces the rule rather than requesting it: a C++ Program that omits it fails to compile, and
    one that throws anyway calls std::terminate at the throw site with its own frames intact rather
    than unwinding into a Grid frame built by a different toolchain. Both windows-msvc and
    windows-mingw are configured presets, so that pairing is a configuration that exists today.

    The rule is about the boundary, not about either codebase. The Grid is built with /EHsc and
    throws by its own style guide; none of that may reach a Program.

    A language whose failures are not C++ exceptions has to arrive at the same place by its own
    route: nothing may unwind out of one of these functions. Rust reaches it without being asked,
    since an unwind out of an `extern "C"` function aborts the process rather than crossing — so
    `panic = "abort"` is worth setting and is not what makes a Rust Program correct. Go, Swift and
    OCaml each have their own answer, and each has to give one.
*/
#ifdef __cplusplus
#define TGL_NOEXCEPT noexcept
#else
#define TGL_NOEXCEPT
#endif

/*! Compile-time assertion. The supported C standard is C17 — ISO/IEC 9899:2018, also written
    C18 — because that is the C standard C++20 itself names as its library baseline, so the Grid's
    side and a C Program's side quote one era of the language rather than two. Anything older is
    refused outright rather than served by a fallback: a pre-C11 toolchain cannot check this
    header's layout assertions readably, and an ABI header that cannot check its own layout is a
    memory corruption with documentation. */
#if defined(__cplusplus)
#define TGL_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define TGL_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#error "The Program ABI requires C17 or later (C++11 or later on the C++ side). There is deliberately no pre-C11 fallback."
#endif

/*! Size of one member without an instance to hand. Unevaluated, so the null pointer is never
    dereferenced; this is the ordinary spelling of the idiom in both C and C++. */
#define TGL_SIZEOF_MEMBER(type, member) sizeof(((type*)0)->member)

TGL_STATIC_ASSERT(sizeof(void*) == 8u, "The Program ABI is 64-bit only. Every offset after the first pointer moves on a 32-bit build.");
TGL_STATIC_ASSERT(sizeof(float) == 4u, "The Program ABI requires a 4-byte float.");

/* ================================================================================================
   What a binding in another language has to agree with

   A Program is a shared library exporting one C symbol, so it can be written in anything that
   compiles to one. Nothing in this file needs a C compiler to express: every member is a float, a
   fixed-width integer or a pointer, and there is not a union, a bitfield, an enum, a `bool`, a
   native `int`, a `size_t` or a packing pragma anywhere in it. That is deliberate and it is the
   whole reason the interface looks so plain.

   Four things a binding must match, none of which it can see from outside:

   1. **The platform's ordinary C calling convention** — SysV AMD64 on Linux, Microsoft x64 on
      Windows. Whatever the platform's C compiler does by default, with no `__stdcall`, no
      `__fastcall` and no vectorcall. There is exactly one such convention per platform here, which
      is why this is a sentence rather than an annotation on every function.
   2. **64-bit.** Asserted above rather than assumed. Every offset past the first pointer moves on a
      32-bit build, and the manifest's offsets would all be wrong.
   3. **C struct layout, naturally aligned, with no padding anywhere.** The second half is asserted
      per struct rather than hoped for, which is what makes the published offsets total the size.
   4. **No unwinding across the boundary**, in either direction. See TGL_NOEXCEPT.

   A language that can consume this header gets the layout checked at compile time by the assertions
   at the bottom. A language that cannot is declaring these structs a second time with nothing
   holding the copies together, so the layout is also published as data in
   `libs/program-abi/abi_layout.txt` — every size and every offset, taken from the compiler. Assert
   against it in the binding's own tests and the drift that would otherwise arrive as nonsense in a
   sensor buffer becomes a failing test instead.
   ================================================================================================ */

/* ================================================================================================
   The body frame

   Right-handed, metres, +X to the creature's right, +Y up, -Z forward. Identical to the Grid's
   world frame and to the debug camera, because this project has one convention rather than two.

   Every three-float array below is (x, y, z) in that order, in body frame. A creature at rest reads
   specific_force = { 0, +9.81, 0 }. Rotation sign is the right-hand rule about each axis, so a
   positive angular_velocity[1] and a positive desired_turn_rate both mean a turn to the creature's
   left seen from above.

   One rigid frame - the head's - so a body-frame coordinate names one place and needs no further
   addressing. Since version 7 a body may be a CHAIN: the model declares how many segments wear it
   and how far apart they sit (TglRenderModel::segment_count, segment_spacing), and the world places
   every trailing segment along the path the head walked - kinematic trail, not articulation. The
   senses still speak of the head alone: nothing here reports a segment's pose or a joint angle,
   because nothing on the Grid bends a joint yet. When a solver that can arrives, joint angle and
   joint rate arrive with it - separate signals, because they are separate receptors in every animal
   that has them - and the version bumps again.

   A Program is never handed a diagram of its own body: no segment lengths, no rest pose, no
   kinematic tree. Nowhere in an animal is there such a model, and the ABI reflects that. What a
   creature gets is where it was touched, how it is moving, and what it can sense, from which the
   shape of itself is something it may learn rather than something it is told.
   ================================================================================================ */

/* ================================================================================================
   Descriptors, handed once at rez and never again
   ================================================================================================ */

/*! Grid-side facts a Program library may want before any creature exists. */
typedef struct TglLibraryInfo {
    /*! How many program_rez calls this library receives this run. The roster is fixed at startup,
        so this is exact rather than a hint and a library may size a pool from it. */
    uint32_t creature_count;

    /*! Seconds per tick. The Grid runs a fixed timestep, so TglSenses::dt_seconds carries this same
        value on every tick of the run. There is deliberately no tick_rate_hz: an integer hertz
        cannot represent every reciprocal dt, so two spellings of one fact would drift. */
    float nominal_dt_seconds;
} TglLibraryInfo;

/*!
    Geometry of one eye. Fixed for the creature's lifetime.

    One layout, and it is a sample list. There is no raster and no field-of-view pair, because not
    one of the six sensor presets in PERCEPTION.md is a perspective raster: a rectangle plus two
    angles cannot express a near-spherical insect field, a rodent's 8.38 steradians, or an
    interommatidial angle that varies severalfold across a single eye. A raster is the degenerate
    sample list whose directions happen to form a grid, so nothing is lost by omitting it.
*/
typedef struct TglEyeDesc {
    /*! Three floats per sample: that sample's unit view direction in body frame, sample_count * 3
        floats in total. Never NULL. Borrowed for the duration of the call. */
    const float* sample_directions;

    /*! One float per sample: that sample's acceptance angle in radians, as a Gaussian full width at
        half maximum. The angle the sample integrates over, and the field that decides whether a
        faithful render blurs or aliases. Never NULL. Borrowed for the duration of the call. */
    const float* sample_acceptance_angles;

    /*! Where this eye sits on the body, in body frame, metres. Two eyes on a head are two positions,
        and that is how a stated binocular overlap becomes expressible. It is also why the simplest
        preset is two eyes of one sample each rather than one eye of two samples: its head and tail
        photoreceptors sit in different places, and a shared origin would collapse the only spatial
        discrimination it has. */
    float position[3];

    /*! Number of samples, not floats. */
    uint32_t sample_count;

    /*! Values per sample. 1 is a scalar; 3 is the usual three-band eye. What each band weights is a
        property of this body, documented per preset in PERCEPTION.md, and is emphatically not
        always RGB. */
    uint32_t channels;

    /*! Distinct levels the Grid quantised this eye's samples to, expressed in bits. The samples
        arrive as floats regardless; this says how much of that range carries information. Zero
        means unquantised. The simplest preset is specified at four bits or fewer, and a Program
        treating it as an eight-bit signal has invented six bits of dynamic range the animal does
        not have. */
    uint32_t quantisation_bits;
} TglEyeDesc;

/*!
    Geometry of one ear. Fixed for the creature's lifetime.

    There is no ear axis. The Grid's gather casts a full spherical direction set from a point, so an
    ear has a position and no directivity whatsoever, and a direction member would be a field the
    Grid reads and nothing acts on. It arrives when directivity does, and the version bumps then.
*/
typedef struct TglEarDesc {
    /*! Band edges in hertz, band_count + 1 values, ascending. Chosen from this body's audiogram
        rather than from room-acoustics convention. Never NULL. Borrowed for the call. */
    const float* band_edges_hz;

    /*! Atmospheric absorption per band, decibels per kilometre, band_count values. Authored per
        body because the bands are per body; the air itself is fixed at 20 C and 70 % relative
        humidity, so these are constants of the Grid resolved into this ear's bands. Never NULL.
        Borrowed for the call. */
    const float* air_absorption_db_per_km;

    /*! Where this ear sits on the body, in body frame, metres. Two ears differ by position alone,
        and the resulting path-length difference is the entire physical basis this ABI offers for
        localisation. Performing the localisation is the Program's job. */
    float position[3];

    uint32_t band_count;

    /*! Time bins in the impulse response delivered each tick. Bin b covers arrival delays
        [b, b + 1) * bin_seconds measured from the start of the tick, so bin 0 is delay zero. The
        span bin_count * bin_seconds is longer than one tick, which is correct rather than an
        oversight: the response is the Grid's answer for the whole flight of a sound that left this
        tick, and an echo of it arrives after this tick. */
    uint32_t bin_count;

    float bin_seconds;
} TglEarDesc;

/*!
    The body this Program will drive. Sensor and actuator geometry only, and no Grid state.

    Note the direction of control: the Grid decides how many eyes and ears a body has, where they
    sit, what they resolve and how hard the body can push. A Program does not request a sense and
    does not request a capability.
*/
typedef struct TglCreatureDesc {
    /*! Stable within this run. For the Program's own logging; it names nothing on the Grid. */
    uint64_t creature_id;

    /*! Deterministic seed for any randomness the Program needs. Derived from the run seed and this
        creature's roster index, so it is stable across runs of the same roster and distinct between
        creatures of it. */
    uint64_t random_seed;

    /*! This body's eyes, in the order their views arrive each tick. NULL when eye_count is zero,
        which is a legitimate body. Borrowed for the duration of the call, as is every array the
        descriptors point at. */
    const TglEyeDesc* eyes;

    /*! This body's ears, same rules. NULL when ear_count is zero, which is the correct
        specification for all three insect presets. */
    const TglEarDesc* ears;

    uint32_t eye_count;
    uint32_t ear_count;

    /*! Directions the Grid integrates to produce `TglSenses::irradiance`, from the same spherical
        Fibonacci set the acoustic gather draws on.

        Declared per body rather than fixed here because it is what the sense costs, and what a body
        can afford differs: it is one ray each, and a creature with no eyes at all may reasonably
        spend more on the only light sense it has than one already tracing a thousand.

        Zero means the body has no thermoreception and `irradiance` reads zero every tick. That is a
        real specification rather than a degenerate one — most presets in PERCEPTION.md have no such
        organ. */
    uint32_t irradiance_sample_count;

    /*! Most contacts TglSenses::contacts can report in one tick. A Program may size a fixed buffer
        from it, the same guarantee TglLibraryInfo::creature_count gives a library.

        It is also a statement that the Grid truncates rather than growing without bound, and what it
        keeps when it must choose: the strongest contacts by impulse magnitude, ties broken by the
        Grid's own contact order, which is fixed for a given tick. Discarding the faintest is both the
        deterministic choice and the biologically right one — an animal being struck hard does not
        lose the blow to notice a graze. */
    uint32_t max_contact_count;

    /*! Magnitude bounds the Grid clamps TglActions to, in the units of the matching action fields,
        all non-negative. A Program is told what its body can do rather than made to discover it by
        experiment, because an animal has that knowledge and it is a fact about the body rather than
        about the Grid. A bound of zero means the body has no such actuator. */
    float max_forward_speed;
    float max_turn_rate;
    float max_vocalisation_strength;

    /*! The servos: the Grid's limit for a joint's swing (radians, the bound every joint target is
        clamped to) and for the torque a servo holds with (newton-metres). Which actuators a body
        has follows from the body it brings: a chain (segment_count above one in its model) is a
        row of servos at its pivots and has no velocity actuator; a body of one segment has the
        velocity actuators and no servos. The descriptor precedes the model, so it states the
        Grid's limit for each class; the Grid tells the world which class this body has once the
        model is validated, and a bound of zero there is no such actuator. */
    float max_joint_angle;
    float max_joint_torque;

    /*! Unused; present so the struct's members account for all of its bytes, which is what the
        padding assertions below demand. Alignment padding, not a reserved capability: the next
        four-byte member simply takes this slot. */
    uint32_t padding0;
} TglCreatureDesc;

/* ================================================================================================
   The render model, handed once at rez — and in the other direction

   This is the one thing in the interface a Program authors rather than receives, and the
   inversion is deliberate. The Grid decides what a body can do and sense, because capability is
   physics and physics is the Grid's; what a body looks like lives with the Program, because a
   creature's shape belongs in the creature's own repository and a Grid that carried every
   creature's mesh would couple its releases to every body ever modelled. A Program rezzes in
   with its own appearance.

   The Grid keeps what it must. The material model is the Grid's — a body is built of the same
   continuous colour / refraction / emission / transmission stuff every surface of the Grid is,
   in linear units, and nothing here can express a texture because nothing on the Grid can. The
   frame is the body frame, in metres. And the Grid validates before it accepts: a model with an
   index out of range, a value that is not a number, or a triangle with no area refuses the rez
   outright, because a NaN vertex in the world's hierarchy would take the Grid down on behalf of
   one Program.

   One rigid piece per segment. Since version 7 the model may declare a chain: `segment_count`
   segments, the head counted, `segment_spacing` metres apart along the head's path, every one
   wearing this same mesh. The world places the trailing segments (a kinematic trail behind the
   head) and the Grid draws the mesh once per segment; the joint between two segments is the
   model's own business - a stub on each of the two spikes that meet, authored into the mesh -
   so nothing here describes a joint. The Grid validates the chain with the rest: a count of
   none or of more than TGL_SEGMENTS_MAX, a single body with a spacing, or a chain without one
   refuses the rez.

   "A Program is never handed a diagram of its own body" survives this section unchanged: the
   Grid still tells a Program nothing about its shape, and the senses still report only what the
   body feels and sees. A Program that authors its model knows only what it chose to write, which
   is the same epistemic position a genome is in.
   ================================================================================================ */

/*! One surface of a body, in the Grid's own material model — the same four quantities every
    surface of the Grid carries, in the same linear units. See docs/MATERIALS.md for what they
    mean; what matters here is only the layout. */
typedef struct TglRenderMaterial {
    /*! Linear tint applied to reflected and transmitted light, (r, g, b). Mirrors are typically
        almost black. */
    float colour[3];

    /*! Refractive index. Drives Fresnel for every surface, and Snell refraction when transmission
        is non-zero. Must be positive and finite. */
    float index_of_refraction;

    /*! Emitted radiance, (r, g, b), watts per steradian per square metre. Non-zero makes the
        surface a light — a body's own neon. */
    float emission[3];

    /*! Fraction of non-reflected light that passes through rather than being absorbed, 0 to 1. */
    float transmission;
} TglRenderMaterial;

/*! One triangle of a body: three vertex indices and the material of its face. */
typedef struct TglRenderTriangle {
    /*! Indices into the model's vertex list, anticlockwise seen from outside the body. */
    uint32_t vertices[3];

    /*! Index into the model's material list. */
    uint32_t material;
} TglRenderTriangle;

/*!
    A body's shape, filled by the Program during program_rez and copied by the Grid before the
    call returns.

    The Grid zeroes this before the call. A Program with no visible body leaves it zeroed —
    triangle_count of zero means no geometry, every other field is then ignored, and that is a
    legitimate body rather than a degenerate one. A Program that provides a model points every
    array at its own storage, borrowed for the duration of the call exactly as the descriptor's
    arrays are borrowed in the other direction.

    A model with any triangle is validated whole before any of it is accepted: vertex_count,
    triangle_count and material_count must all be non-zero with their pointers non-NULL, every
    index must be in range, every value finite, the index of refraction positive, transmission
    within zero and one, and every triangle must have area. A model that fails any of it refuses
    the whole rez, loudly, because the alternative is a poisoned world hierarchy that fails
    somewhere else entirely.
*/
typedef struct TglRenderModel {
    /*! Vertex positions in body frame, metres: three floats per vertex, vertex_count * 3 in
        total. NULL when triangle_count is zero. */
    const float* vertex_positions;

    /*! The triangles. NULL when triangle_count is zero. */
    const TglRenderTriangle* triangles;

    /*! The body's materials. NULL when triangle_count is zero. */
    const TglRenderMaterial* materials;

    /*! Number of vertices, not floats. */
    uint32_t vertex_count;

    /*! Number of triangles. Zero means the body has no visible shape. */
    uint32_t triangle_count;

    /*! Number of materials. */
    uint32_t material_count;

    /*! Segments in the chain this model dresses, the head counted: 1 for a single rigid body, at
        most TGL_SEGMENTS_MAX. Ignored, like every other field, when triangle_count is zero: a
        chain of nothing is nothing. */
    uint32_t segment_count;

    /*! Metres between consecutive segments' origins along the head's path. Zero for a chain of
        one - a spacing on a single body refuses the rez - and strictly positive and finite
        otherwise. A worm of icosahedra joined spike to spike sets this to the spike-to-spike
        distance plus the joint's length. */
    float segment_spacing;

    /*! Unused; present so the struct's members account for all of its bytes. Alignment padding,
        not a reserved capability. */
    uint32_t padding0;
} TglRenderModel;

/*! The most segments a chain may have, the head counted. The same number as the wire's
    LNK_SEGMENTS_MAX, and the Grid holds the two together with a static assertion where it
    speaks the wire. */
#define TGL_SEGMENTS_MAX 8u

/* ================================================================================================
   Views, handed every tick and valid only for the duration of one program_tick call
   ================================================================================================ */

/*!
    One eye's samples for this tick.

    sample_count and channels are repeated from the matching TglEyeDesc so that indexing this buffer
    needs nothing else, and so a Program can bounds-check without trusting its own copy. Interpreting
    the numbers still needs the descriptor: which direction sample i looked in, over what acceptance
    angle, and what its channels weight are properties of the body and live where the body is
    described. The Grid asserts the repeated values against the descriptor as it fills them.
*/
typedef struct TglEyeView {
    /*! Linear-space sample values, channels floats per sample, sample i channel c at
        samples[(i * channels) + c]. Un-tone-mapped and NOT clamped to [0, 1]: a sample looking
        straight at a neon tube reads well above one. 16-byte aligned. Never NULL. */
    const float* samples;

    /*! Number of samples, not floats. Equals the matching TglEyeDesc::sample_count. */
    uint32_t sample_count;

    /*! Equals the matching TglEyeDesc::channels. */
    uint32_t channels;
} TglEyeView;

/*! The most discrete arrivals one ear reports per tick: the direct path and the loudest images. */
#define TGL_EAR_ARRIVALS_MAX 16u

/*!
    One discrete arrival of a call at this ear, beyond its place in the histogram - the physical
    basis of direction and of Doppler, delivered as data so the Program may extract what its body
    is built to extract (Ormia's ears do it mechanically; a nematode's cannot at all).

    The onset is exact: the path from the caller to this ear over the speed of sound, in seconds,
    with the sub-millisecond structure the histogram's bins destroy - two ears a few centimetres
    apart hear one call tens of microseconds apart, and that difference is where the caller is.
    The radial velocity is how fast that path lengthens, positive receding and negative
    approaching; a Program senses Doppler from it (under one per cent of pitch at creature speeds,
    which is why it travels as a number rather than as pre-shifted energies). The energies are what
    the arrival deposited in each of this ear's bands. The ambient hum records none of this: a
    sustained, sourceless bed has no onset to time and no bearing worth naming.
*/
typedef struct TglArrival {
    float onset_seconds;
    float radial_velocity;
    float energy[4]; /*!< Per band, band_count of them meaningful, the rest zero. */
} TglArrival;

/*! One ear's impulse response for this tick. */
typedef struct TglEarView {
    /*! Energy per (band, bin), band-major: element [(band * bin_count) + bin], and therefore
        band_count * bin_count floats in total. 16-byte aligned. Never NULL.

        Band-major because that is what a listener walks: finding arrival times within a band means
        stepping through bins, and this layout makes that one contiguous run per band.

        The unit is the primary neon tube, whose authored strength is 1.0 by definition. That is the
        Grid's one reference level, it is the same reference TglActions::vocalisation_strength uses,
        and a bin is a sum over every arrival from every source in those units. A bin no sound has
        reached reads zero, which is the physical answer rather than a sentinel. */
    const float* energy;

    /*! The discrete arrivals this tick, arrival_count of them, at most TGL_EAR_ARRIVALS_MAX, in the
        order the world delivered them. NULL when arrival_count is 0 - a silent tick, or an ear
        hearing the hum alone. Borrowed for the program_tick call, like everything here. */
    const TglArrival* arrivals;
    uint32_t arrival_count;

    /*! Equals the matching TglEarDesc::band_count. Named first because the index is band-major. */
    uint32_t band_count;

    /*! Equals the matching TglEarDesc::bin_count. */
    uint32_t bin_count;

    /*! Always zero. Named so the asserts can count it: the pointer above needs the struct eight-
        byte aligned, and an invisible tail of padding is exactly what this header refuses. */
    uint32_t reserved0;
} TglEarView;

/*!
    One place the body was touched this tick.

    A list rather than a single summed vector, because summing destroys the one thing touch is for.
    A body lying along the floor contacts it in many places at once, and the sum of those is a number
    that says "downwards" and nothing about lying down. Where matters more than how much.

    The position is resolved in the body frame as the body actually is this tick, so nothing about it
    requires a Program to know its own shape. It is also how a Program may come to know it: an animal
    learns the extent of itself by bumping into the world, and this is the only sense here that
    reports a point *on* the creature rather than a direction away from it.
*/
typedef struct TglContact {
    /*! Where the contact happened, body frame, metres. */
    float position[3];

    /*! Impulse delivered to the body at that point, newton-seconds, in body frame. Direction is the
        direction the body was pushed; magnitude is how hard. Never zero: a contact carrying no
        impulse is not reported at all, so a Program need not filter them. */
    float impulse[3];

    /*! The face that touched, as the world has it: its unit normal in the WORLD frame - which way
        the face pushes. The floor says { 0, 1, 0 }; a wall says which wall; another creature says
        where it stands. A creature that knows its own yaw can turn this into its own frame; one that
        does not still learns that something is above, beside, or beneath. */
    float normal[3];

    /*! Metres the body stood past the face before the world stood it back; zero for a body merely
        resting. A landing or a bump reads as depth, a stance reads as none. */
    float depth;

    /*! The body's velocity along the face at this contact, body frame, metres per second: the slip.
        Feet that walk slip on the floor; a flank dragged along a wall slips along the wall; a body
        brushed by another slips relative to it. Zero for a foot planted and a body at rest. This is
        the sense of scraping, and the sound of it is what the Grid's SCRATCH event carries. */
    float slip[3];
} TglContact;

/*!
    Everything a creature perceives this tick. Every pointer in it, and every pointer reachable
    through it, is borrowed for the duration of the program_tick call and invalid afterwards.

    No address the Grid hands over is stable or meaningful. A Program that hashes one, keys a cache
    on one, or compares one against last tick's has left the reproducible set: addresses vary
    between runs under address-space randomisation, and the replay path deliberately supplies
    different ones for the same recorded tick.

    Members are ordered widest first, so the struct has no padding on either supported platform.
    That is worth more than grouping by modality, because a struct with holes has indeterminate
    bytes and is therefore neither hashable nor recordable byte-wise, and this repository hashes
    things. The modality grouping survives as comments, which is where legibility belongs.
*/
typedef struct TglSenses {
    /*! Ticks since the run began, shared by every creature, so a creature rezzed later sees the
        Grid's counter rather than its own age and two recordings line up. */
    uint64_t tick;

    /* -- Vision and hearing ------------------------------------------------------------------- */

    /*! One view per eye, in the same order as TglCreatureDesc::eyes. NULL when eye_count is 0. */
    const TglEyeView* eyes;

    /*! One view per ear, in the same order as TglCreatureDesc::ears. NULL when ear_count is 0. */
    const TglEarView* ears;

    /* -- Touch ---------------------------------------------------------------------------------- */

    /*! Contacts this tick, at most TglCreatureDesc::max_contact_count of them. NULL when
        contact_count is 0, which is the ordinary case for a creature touching nothing. Order is the
        Grid's own contact order and is fixed for a given tick, so a recording replays identically;
        it carries no other meaning, and in particular it is not sorted by strength. */
    const TglContact* contacts;

    uint32_t eye_count;
    uint32_t ear_count;
    uint32_t contact_count;

    /*! Duration of this tick in seconds, equal to TglLibraryInfo::nominal_dt_seconds on every tick.
        The Grid runs a fixed timestep and holds no wall clock inside the simulation. */
    float dt_seconds;

    /* -- Proprioception: what the body's own actuators report about themselves ----------------- */

    float body_forward_speed; /*!< Metres per second along -Z. Negative is reversing. */
    float body_vertical_speed; /*!< Metres per second along +Y. */
    float body_turn_rate; /*!< Radians per second about +Y, right-handed. */

    /*! The angle each servo holds this tick, radians, joint k between segments k and k + 1 in
        the sign TglActions::joint_targets asks in, within a turn; segment_count - 1 meaningful,
        the rest zero, all zero for a body of one segment. The encoder's reading - what the joint
        did, not what it was asked: it lags a target the servo is still swinging to, stops short
        of one past the servo's torque, and gives to a wall, a neighbour or the floor's grip on a
        runner - and that difference is the only readback a gait has, as body_forward_speed is
        for the velocity actuator. Reported by the world that holds the servos, in the same
        letter as the specific force and the contacts; the Grid copies it and derives nothing. */
    float joint_angles[TGL_SEGMENTS_MAX - 1u];

    /*! The torque each servo holds its angle with at the tick's end, newton-metres, signed in
        the angle's sense: at most max_joint_torque in magnitude, and exactly that when the servo
        stalls - against a wall, a neighbour, the floor's grip on a runner. What organ: a motor's
        current sense, a tendon's organ; the load a joint bears, which no pose can yield, so the
        world that holds the servos reports it in the same letter as the angles. Same slots as
        joint_angles; zero beyond the chain and for a body of one segment. */
    float joint_torques[TGL_SEGMENTS_MAX - 1u];

    /* -- Vestibular ---------------------------------------------------------------------------- */

    /*! Specific force in body frame, metres per second squared: linear acceleration with gravity
        included, exactly what an otolith senses. At rest this reads { 0, +9.81, 0 }. Gravity and
        acceleration are conflated in one number because they are conflated in the animal, which is
        precisely why a creature can be fooled about which way is down. */
    float specific_force[3];

    /*! Angular velocity about the body axes, radians per second, the semicircular-canal analogue.
        Sensed inertially, which is not the commanded turn rate above: the two disagree whenever the
        body is pushed rather than driven. */
    float angular_velocity[3];

    /* -- Thermoreception ------------------------------------------------------------------------ */

    /*! Radiance arriving at the creature's position, integrated over the whole sphere, in the
        renderer's linear units and with no directional resolution whatsoever. Strictly this is a
        spherical fluence rate rather than an irradiance, which is hemispherical and cosine-weighted;
        the name is inherited from PERCEPTION.md and ARCHITECTURE.md.

        Deterministic: a fixed quadrature over the same spherical Fibonacci set the acoustic gather
        uses, not a single sample. Nothing on the Grid samples randomly, and a one-ray sphere
        integral would be exactly that. Faithful rather than cheapened, too: a pit viper's infrared
        organ is likewise a very low-resolution radiance sensor, and it is enough to tell warm from
        cold long before it is enough to see. */
    float irradiance;

    /*! Always zero. Two words that round the struct to its alignment so that no byte of it is
        unnamed; a Program never reads them. */
    uint32_t padding0[2];
} TglSenses;

/*!
    What the creature attempts this tick, and the only memory a Program may write across the
    boundary. The Grid zeroes it before every call, so a Program that writes nothing coasts to a stop
    rather than repeating last tick or reading whatever was on the stack.

    Every field is physical intent in the body frame. The Grid replaces non-finite values with zero,
    clamps to the matching TglCreatureDesc bound, and feeds the result to the physics step as intent
    rather than as a teleport. Sanitise precedes clamp, in that order and no other, because most
    hand-written clamps return NaN for a NaN input and a NaN velocity becomes a NaN position and then
    a BVH traversal that does not terminate.
*/
typedef struct TglActions {
    /*! Metres per second along -Z. Negative reverses. Clamped to +/- max_forward_speed. Traction
        is a fact about contact: the intent moves the body only while it stands on something, and a
        body in flight keeps the velocity it left the ground with. */
    float desired_forward_speed;

    /*! Radians per second about +Y, right-handed, so positive turns to the creature's left seen from
        above. Clamped to +/- max_turn_rate. */
    float desired_turn_rate;

    /*! There is deliberately no vertical intent. Height is physics' business — gravity, the floor
        and whatever the body ran off — and on a Grid with no water and nothing climbable a vertical
        actuator clamped to zero for every plausible body was a field the Grid read and nothing
        could act on. `TglSenses::body_vertical_speed` still reports what gravity is doing. */

    /*! Loudness of one call emitted this tick, in the unit TglEarView::energy uses: relative to a
        primary neon tube, whose authored strength is 1.0 by definition. Zero is silent, a negative
        value is treated as zero, and the result is clamped to max_vocalisation_strength.

        A call, not a channel: the Grid emits a single burst from the creature's own position and it
        is over. No duration, spectrum or waveform, because there is no waveform anywhere on the
        Grid. What a voice sounds like is a fact about the body, authored on the Grid's side of this
        boundary as the ears are; this says only whether the voice was used, and how hard.

        Like every action it is staged: the call sounds on the next tick, and that tick's ear views
        carry its whole response — the caller's own ears included, loudest and first, which is the
        only readback the voice has or needs. */
    float vocalisation_strength;

    /*! The angle each servo is asked to hold this tick, radians, joint k between segments k and
        k + 1, positive bending the chain to the creature's left; segment_count - 1 meaningful, the
        rest zero. Clamped to +/- max_joint_angle, held with no more than max_joint_torque - past
        that a servo stalls. The muscle: which joint bends when is the Program's own gait, and
        the Grid carries it to the world as it is. A body of one segment has none. */
    float joint_targets[TGL_SEGMENTS_MAX - 1u];
} TglActions;

/* ================================================================================================
   The vtable and the one exported symbol
   ================================================================================================ */

/*! Opaque per-creature Program state. The Grid never dereferences it and never frees it. */
typedef struct TglProgram TglProgram;

/*!
    Function pointers a Program library provides to the Grid.

    This is the one struct in the interface the Program allocates and the Grid reads, and that
    inversion is why it is the one struct with a size field. Every other struct here is allocated by
    the Grid, so the Grid may grow it at the tail and an older Program simply never looks at the new
    part. Grow this one without a size field and the Grid reads past the end of the Program's static
    object and then calls through whatever its linker placed next, which usually appears to work
    because that memory is usually zero.

    struct_size cannot be retrofitted: adding it later moves every other member and breaks every
    Program that exists. Four bytes, one line in TGL_PROGRAM_VTABLE_HEADER and one comparison in the
    loader. It is the only piece of compatibility machinery in this file, and it is bought against a
    named future need rather than a present one: a main-thread pump member, which Qt and GTK
    diagnostic GUIs would require and which VST3 and CLAP both had to add after the fact.
*/
typedef struct TglProgramVTable {
    /*! sizeof(TglProgramVTable) as the Program was compiled. Set by TGL_PROGRAM_VTABLE_HEADER. */
    uint32_t struct_size;

    /*! TGL_ABI_VERSION as the Program was compiled. Checked in addition to the value passed to
        tglGetProgramVTable, because a hand-written binding in another language may ignore its
        argument and must still fail loudly rather than silently. */
    uint32_t abi_version;

    /* -- Library scope: plain names, because these are dlopen and dlclose, facts about the
          operating system rather than events on the Grid. ------------------------------------- */

    /*! Called once after the library loads, before any Program is rezzed, on the roster thread.
        Must not be NULL. */
    void (*library_init)(const TglLibraryInfo* info) TGL_NOEXCEPT;

    /* -- Program scope -------------------------------------------------------------------------- */

    /*! Rezzes one Program onto one body. Returns NULL on failure. Roster thread. Must not be NULL.

        `model` is the Program's to fill and the Grid's to validate — see TglRenderModel. The Grid
        zeroes it before the call and copies what it accepts before the call returns; a Program
        that leaves it zeroed has no visible body, which is a legitimate body and today's default. */
    TglProgram* (*program_rez)(const TglCreatureDesc* desc, TglRenderModel* model)TGL_NOEXCEPT;

    /*! Called once per tick per live creature, serially, in roster order, on one thread whose
        identity is fixed for the whole run. Must not block. Must not be NULL. */
    void (*program_tick)(TglProgram* program, const TglSenses* senses, TglActions* actions) TGL_NOEXCEPT;

    /*! Derezzes one Program. The handle is invalid afterwards. Roster thread. Must not be NULL. */
    void (*program_derez)(TglProgram* program) TGL_NOEXCEPT;

    /*! Called once, after every handle has been derezzed, on the roster thread. The last chance to
        destroy windows, unregister window classes and join threads. Must not be NULL. */
    void (*library_shutdown)(void) TGL_NOEXCEPT;
} TglProgramVTable;

/*! Fills the two header members. Use as the first initialiser of a static vtable. */
#define TGL_PROGRAM_VTABLE_HEADER (uint32_t)sizeof(TglProgramVTable), (uint32_t)TGL_ABI_VERSION

/*! Smallest vtable the Grid accepts: the header plus the five members required at version 1. This
    is a floor and never an equality, so that appending a member stays compatible. */
#define TGL_PROGRAM_VTABLE_MIN_SIZE 48u

/*!
    The single symbol every Program library exports, with C linkage and this exact name.

    The Grid passes TGL_ABI_VERSION as the Grid was built. A Program returns NULL if it cannot
    satisfy that version, and the Grid then logs both numbers and refuses to start the run. No
    negotiation and no shims.

    The returned pointer must have static storage duration and remain valid until after
    library_shutdown returns. The Grid never frees it.
*/
TGL_PROGRAM_EXPORT const TglProgramVTable* tglGetProgramVTable(uint32_t abi_version) TGL_NOEXCEPT;

/*! Type of the above, for the Grid's dlsym / GetProcAddress cast. Declared here so that the cast
    target is not a second, uncompiled copy of a signature that already exists in this file. */
typedef const TglProgramVTable* (*TglGetProgramVTableFn)(uint32_t abi_version)TGL_NOEXCEPT;

/* ================================================================================================
   Layout, pinned

   These fire in a Program's build as well as the Grid's, which is the whole point: a header that has
   drifted fails to compile on the side that vendored it, rather than corrupting memory on the side
   that loaded it. A comment saying "must equal" is not a mechanism.

   Every struct is pinned three ways, and the third is not redundant. Sizes and offsets alone leave a
   hole: narrow a member from uint64_t to uint32_t immediately before a pointer and the four bytes it
   gives up are absorbed by the padding that aligns the pointer, so every offset and every size holds
   while the meaning of the bytes has changed underneath. A Program built against the old header then
   reads eight bytes where the Grid writes four, and nothing anywhere complains. So each struct also
   asserts that its members account for all of it — which is the "no padding" claim made above in
   prose, turned into something the compiler checks, and which no such change can survive.
   ================================================================================================ */

#define TGL_SUM2(t, a, b) (TGL_SIZEOF_MEMBER(t, a) + TGL_SIZEOF_MEMBER(t, b))
#define TGL_SUM3(t, a, b, c) (TGL_SUM2(t, a, b) + TGL_SIZEOF_MEMBER(t, c))
#define TGL_SUM4(t, a, b, c, d) (TGL_SUM3(t, a, b, c) + TGL_SIZEOF_MEMBER(t, d))
#define TGL_SUM5(t, a, b, c, d, e) (TGL_SUM4(t, a, b, c, d) + TGL_SIZEOF_MEMBER(t, e))
#define TGL_SUM6(t, a, b, c, d, e, f) (TGL_SUM4(t, a, b, c, d) + TGL_SUM2(t, e, f))
#define TGL_SUM7(t, a, b, c, d, e, f, g) (TGL_SUM4(t, a, b, c, d) + TGL_SUM3(t, e, f, g))
#define TGL_SUM9(t, a, b, c, d, e, f, g, h, i) (TGL_SUM4(t, a, b, c, d) + TGL_SUM5(t, e, f, g, h, i))
#define TGL_SUM12(t, a, b, c, d, e, f, g, h, i, j, k, l) (TGL_SUM6(t, a, b, c, d, e, f) + TGL_SUM6(t, g, h, i, j, k, l))
#define TGL_SUM14(t, a, b, c, d, e, f, g, h, i, j, k, l, m, n) (TGL_SUM7(t, a, b, c, d, e, f, g) + TGL_SUM7(t, h, i, j, k, l, m, n))
#define TGL_SUM15(t, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) (TGL_SUM14(t, a, b, c, d, e, f, g, h, i, j, k, l, m, n) + TGL_SIZEOF_MEMBER(t, o))
#define TGL_SUM16(t, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) (TGL_SUM15(t, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) + TGL_SIZEOF_MEMBER(t, p))
#define TGL_SUM17(t, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q) (TGL_SUM16(t, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) + TGL_SIZEOF_MEMBER(t, q))

TGL_STATIC_ASSERT(TGL_SUM2(TglLibraryInfo, creature_count, nominal_dt_seconds) == sizeof(TglLibraryInfo), "TglLibraryInfo has padding: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM6(TglEyeDesc, sample_directions, sample_acceptance_angles, position, sample_count, channels, quantisation_bits) == sizeof(TglEyeDesc),
    "TglEyeDesc has padding: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM6(TglEarDesc, band_edges_hz, air_absorption_db_per_km, position, band_count, bin_count, bin_seconds) == sizeof(TglEarDesc),
    "TglEarDesc has padding: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM14(TglCreatureDesc, creature_id, random_seed, eyes, ears, eye_count, ear_count, irradiance_sample_count, max_contact_count, max_forward_speed,
                      max_turn_rate, max_vocalisation_strength, max_joint_angle, max_joint_torque, padding0)
        == sizeof(TglCreatureDesc),
    "TglCreatureDesc has padding beyond its named padding member: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM4(TglRenderMaterial, colour, index_of_refraction, emission, transmission) == sizeof(TglRenderMaterial),
    "TglRenderMaterial has padding: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM2(TglRenderTriangle, vertices, material) == sizeof(TglRenderTriangle), "TglRenderTriangle has padding: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM9(TglRenderModel, vertex_positions, triangles, materials, vertex_count, triangle_count, material_count, segment_count, segment_spacing, padding0)
        == sizeof(TglRenderModel),
    "TglRenderModel has padding beyond its named padding member: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM3(TglEyeView, samples, sample_count, channels) == sizeof(TglEyeView), "TglEyeView has padding: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM6(TglEarView, energy, arrivals, arrival_count, band_count, bin_count, reserved0) == sizeof(TglEarView),
    "TglEarView has padding: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM3(TglArrival, onset_seconds, radial_velocity, energy) == sizeof(TglArrival), "TglArrival has padding: a member changed width.");
TGL_STATIC_ASSERT(sizeof(TglArrival) == 24u, "TglArrival is an array element, so its size is a stride. It must be 24 bytes.");
TGL_STATIC_ASSERT(TGL_SUM5(TglContact, position, impulse, normal, depth, slip) == sizeof(TglContact), "TglContact has padding: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM17(TglSenses, tick, eyes, ears, contacts, eye_count, ear_count, contact_count, dt_seconds, body_forward_speed, body_vertical_speed,
                      body_turn_rate, joint_angles, joint_torques, specific_force, angular_velocity, irradiance, padding0)
        == sizeof(TglSenses),
    "TglSenses has padding: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM4(TglActions, desired_forward_speed, desired_turn_rate, vocalisation_strength, joint_targets) == sizeof(TglActions),
    "TglActions has padding: a member changed width.");
TGL_STATIC_ASSERT(TGL_SUM7(TglProgramVTable, struct_size, abi_version, library_init, program_rez, program_tick, program_derez, library_shutdown)
        == sizeof(TglProgramVTable),
    "TglProgramVTable has padding: a member changed width.");

TGL_STATIC_ASSERT(sizeof(TglLibraryInfo) == 8u, "TglLibraryInfo must be 8 bytes with no padding.");
TGL_STATIC_ASSERT(offsetof(TglLibraryInfo, nominal_dt_seconds) == 4u, "TglLibraryInfo::nominal_dt_seconds must sit at offset 4.");

TGL_STATIC_ASSERT(sizeof(TglEyeDesc) == 40u, "TglEyeDesc must be 40 bytes with no padding.");
TGL_STATIC_ASSERT(offsetof(TglEyeDesc, sample_directions) == 0u, "TglEyeDesc::sample_directions must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglEyeDesc, sample_acceptance_angles) == 8u, "TglEyeDesc::sample_acceptance_angles must sit at offset 8.");
TGL_STATIC_ASSERT(offsetof(TglEyeDesc, position) == 16u, "TglEyeDesc::position must sit at offset 16.");
TGL_STATIC_ASSERT(offsetof(TglEyeDesc, sample_count) == 28u, "TglEyeDesc::sample_count must sit at offset 28.");
TGL_STATIC_ASSERT(offsetof(TglEyeDesc, channels) == 32u, "TglEyeDesc::channels must sit at offset 32.");
TGL_STATIC_ASSERT(offsetof(TglEyeDesc, quantisation_bits) == 36u, "TglEyeDesc::quantisation_bits must sit at offset 36.");

TGL_STATIC_ASSERT(sizeof(TglEarDesc) == 40u, "TglEarDesc must be 40 bytes with no padding.");
TGL_STATIC_ASSERT(offsetof(TglEarDesc, band_edges_hz) == 0u, "TglEarDesc::band_edges_hz must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglEarDesc, air_absorption_db_per_km) == 8u, "TglEarDesc::air_absorption_db_per_km must sit at offset 8.");
TGL_STATIC_ASSERT(offsetof(TglEarDesc, position) == 16u, "TglEarDesc::position must sit at offset 16.");
TGL_STATIC_ASSERT(offsetof(TglEarDesc, band_count) == 28u, "TglEarDesc::band_count must sit at offset 28.");
TGL_STATIC_ASSERT(offsetof(TglEarDesc, bin_count) == 32u, "TglEarDesc::bin_count must sit at offset 32.");
TGL_STATIC_ASSERT(offsetof(TglEarDesc, bin_seconds) == 36u, "TglEarDesc::bin_seconds must sit at offset 36.");

TGL_STATIC_ASSERT(sizeof(TglCreatureDesc) == 72u, "TglCreatureDesc must be 72 bytes with no padding.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, creature_id) == 0u, "TglCreatureDesc::creature_id must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, random_seed) == 8u, "TglCreatureDesc::random_seed must sit at offset 8.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, eyes) == 16u, "TglCreatureDesc::eyes must sit at offset 16.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, ears) == 24u, "TglCreatureDesc::ears must sit at offset 24.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, eye_count) == 32u, "TglCreatureDesc::eye_count must sit at offset 32.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, ear_count) == 36u, "TglCreatureDesc::ear_count must sit at offset 36.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, irradiance_sample_count) == 40u, "TglCreatureDesc::irradiance_sample_count must sit at offset 40.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, max_contact_count) == 44u, "TglCreatureDesc::max_contact_count must sit at offset 44.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, max_forward_speed) == 48u, "TglCreatureDesc::max_forward_speed must sit at offset 48.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, max_turn_rate) == 52u, "TglCreatureDesc::max_turn_rate must sit at offset 52.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, max_vocalisation_strength) == 56u, "TglCreatureDesc::max_vocalisation_strength must sit at offset 56.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, max_joint_angle) == 60u, "TglCreatureDesc::max_joint_angle must sit at offset 60.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, max_joint_torque) == 64u, "TglCreatureDesc::max_joint_torque must sit at offset 64.");
TGL_STATIC_ASSERT(offsetof(TglCreatureDesc, padding0) == 68u, "TglCreatureDesc::padding0 must sit at offset 68.");

TGL_STATIC_ASSERT(sizeof(TglRenderMaterial) == 32u, "TglRenderMaterial is an array element, so its size is a stride. It must be 32 bytes with no padding.");
TGL_STATIC_ASSERT(offsetof(TglRenderMaterial, colour) == 0u, "TglRenderMaterial::colour must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglRenderMaterial, index_of_refraction) == 12u, "TglRenderMaterial::index_of_refraction must sit at offset 12.");
TGL_STATIC_ASSERT(offsetof(TglRenderMaterial, emission) == 16u, "TglRenderMaterial::emission must sit at offset 16.");
TGL_STATIC_ASSERT(offsetof(TglRenderMaterial, transmission) == 28u, "TglRenderMaterial::transmission must sit at offset 28.");

TGL_STATIC_ASSERT(sizeof(TglRenderTriangle) == 16u, "TglRenderTriangle is an array element, so its size is a stride. It must be 16 bytes with no padding.");
TGL_STATIC_ASSERT(offsetof(TglRenderTriangle, vertices) == 0u, "TglRenderTriangle::vertices must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglRenderTriangle, material) == 12u, "TglRenderTriangle::material must sit at offset 12.");

TGL_STATIC_ASSERT(sizeof(TglRenderModel) == 48u, "TglRenderModel must be 48 bytes with no padding beyond its named padding member.");
TGL_STATIC_ASSERT(offsetof(TglRenderModel, vertex_positions) == 0u, "TglRenderModel::vertex_positions must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglRenderModel, triangles) == 8u, "TglRenderModel::triangles must sit at offset 8.");
TGL_STATIC_ASSERT(offsetof(TglRenderModel, materials) == 16u, "TglRenderModel::materials must sit at offset 16.");
TGL_STATIC_ASSERT(offsetof(TglRenderModel, vertex_count) == 24u, "TglRenderModel::vertex_count must sit at offset 24.");
TGL_STATIC_ASSERT(offsetof(TglRenderModel, triangle_count) == 28u, "TglRenderModel::triangle_count must sit at offset 28.");
TGL_STATIC_ASSERT(offsetof(TglRenderModel, material_count) == 32u, "TglRenderModel::material_count must sit at offset 32.");
TGL_STATIC_ASSERT(offsetof(TglRenderModel, segment_count) == 36u, "TglRenderModel::segment_count must sit at offset 36.");
TGL_STATIC_ASSERT(offsetof(TglRenderModel, segment_spacing) == 40u, "TglRenderModel::segment_spacing must sit at offset 40.");
TGL_STATIC_ASSERT(offsetof(TglRenderModel, padding0) == 44u, "TglRenderModel::padding0 must sit at offset 44.");

TGL_STATIC_ASSERT(sizeof(TglEyeView) == 16u, "TglEyeView is an array element, so its size is a stride. It must be 16 bytes.");
TGL_STATIC_ASSERT(offsetof(TglEyeView, samples) == 0u, "TglEyeView::samples must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglEyeView, sample_count) == 8u, "TglEyeView::sample_count must sit at offset 8.");
TGL_STATIC_ASSERT(offsetof(TglEyeView, channels) == 12u, "TglEyeView::channels must sit at offset 12.");

TGL_STATIC_ASSERT(sizeof(TglEarView) == 32u, "TglEarView is an array element, so its size is a stride. It must be 32 bytes.");
TGL_STATIC_ASSERT(offsetof(TglEarView, energy) == 0u, "TglEarView::energy must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglEarView, arrivals) == 8u, "TglEarView::arrivals must sit at offset 8.");
TGL_STATIC_ASSERT(offsetof(TglEarView, arrival_count) == 16u, "TglEarView::arrival_count must sit at offset 16.");
TGL_STATIC_ASSERT(offsetof(TglEarView, band_count) == 20u, "TglEarView::band_count must sit at offset 20.");
TGL_STATIC_ASSERT(offsetof(TglEarView, bin_count) == 24u, "TglEarView::bin_count must sit at offset 24.");
TGL_STATIC_ASSERT(offsetof(TglEarView, reserved0) == 28u, "TglEarView::reserved0 must sit at offset 28.");

TGL_STATIC_ASSERT(sizeof(TglContact) == 52u, "TglContact is an array element, so its size is a stride. It must be 52 bytes with no padding.");
TGL_STATIC_ASSERT(offsetof(TglContact, normal) == 24u, "TglContact::normal must follow the impulse.");
TGL_STATIC_ASSERT(offsetof(TglContact, depth) == 36u, "TglContact::depth must follow the normal.");
TGL_STATIC_ASSERT(offsetof(TglContact, slip) == 40u, "TglContact::slip must follow the depth.");
TGL_STATIC_ASSERT(offsetof(TglContact, position) == 0u, "TglContact::position must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglContact, impulse) == 12u, "TglContact::impulse must sit at offset 12.");

TGL_STATIC_ASSERT(sizeof(TglSenses) == 152u, "TglSenses must be 152 bytes with no padding beyond its named padding member.");
TGL_STATIC_ASSERT(offsetof(TglSenses, tick) == 0u, "TglSenses::tick must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglSenses, eyes) == 8u, "TglSenses::eyes must sit at offset 8.");
TGL_STATIC_ASSERT(offsetof(TglSenses, ears) == 16u, "TglSenses::ears must sit at offset 16.");
TGL_STATIC_ASSERT(offsetof(TglSenses, contacts) == 24u, "TglSenses::contacts must sit at offset 24.");
TGL_STATIC_ASSERT(offsetof(TglSenses, eye_count) == 32u, "TglSenses::eye_count must sit at offset 32.");
TGL_STATIC_ASSERT(offsetof(TglSenses, ear_count) == 36u, "TglSenses::ear_count must sit at offset 36.");
TGL_STATIC_ASSERT(offsetof(TglSenses, contact_count) == 40u, "TglSenses::contact_count must sit at offset 40.");
TGL_STATIC_ASSERT(offsetof(TglSenses, dt_seconds) == 44u, "TglSenses::dt_seconds must sit at offset 44.");
TGL_STATIC_ASSERT(offsetof(TglSenses, body_forward_speed) == 48u, "TglSenses::body_forward_speed must sit at offset 48.");
TGL_STATIC_ASSERT(offsetof(TglSenses, body_vertical_speed) == 52u, "TglSenses::body_vertical_speed must sit at offset 52.");
TGL_STATIC_ASSERT(offsetof(TglSenses, body_turn_rate) == 56u, "TglSenses::body_turn_rate must sit at offset 56.");
TGL_STATIC_ASSERT(offsetof(TglSenses, joint_angles) == 60u, "TglSenses::joint_angles must sit at offset 60.");
TGL_STATIC_ASSERT(offsetof(TglSenses, joint_torques) == 88u, "TglSenses::joint_torques must sit at offset 88.");
TGL_STATIC_ASSERT(offsetof(TglSenses, specific_force) == 116u, "TglSenses::specific_force must sit at offset 116.");
TGL_STATIC_ASSERT(offsetof(TglSenses, angular_velocity) == 128u, "TglSenses::angular_velocity must sit at offset 128.");
TGL_STATIC_ASSERT(offsetof(TglSenses, irradiance) == 140u, "TglSenses::irradiance must sit at offset 140.");
TGL_STATIC_ASSERT(offsetof(TglSenses, padding0) == 144u, "TglSenses::padding0 must sit at offset 144.");

TGL_STATIC_ASSERT(sizeof(TglActions) == 40u, "TglActions must be 40 bytes: three actuators and seven servo targets.");
TGL_STATIC_ASSERT(offsetof(TglActions, desired_forward_speed) == 0u, "TglActions::desired_forward_speed must sit at offset 0.");
TGL_STATIC_ASSERT(offsetof(TglActions, desired_turn_rate) == 4u, "TglActions::desired_turn_rate must sit at offset 4.");
TGL_STATIC_ASSERT(offsetof(TglActions, vocalisation_strength) == 8u, "TglActions::vocalisation_strength must sit at offset 8.");
TGL_STATIC_ASSERT(offsetof(TglActions, joint_targets) == 12u, "TglActions::joint_targets must sit at offset 12.");

TGL_STATIC_ASSERT(sizeof(TglProgramVTable) >= TGL_PROGRAM_VTABLE_MIN_SIZE, "TglProgramVTable must be at least TGL_PROGRAM_VTABLE_MIN_SIZE bytes.");
TGL_STATIC_ASSERT(offsetof(TglProgramVTable, struct_size) == 0u, "TglProgramVTable::struct_size must be first, for ever.");
TGL_STATIC_ASSERT(offsetof(TglProgramVTable, abi_version) == 4u, "TglProgramVTable::abi_version must sit at offset 4.");
TGL_STATIC_ASSERT(offsetof(TglProgramVTable, library_init) == 8u, "TglProgramVTable::library_init must sit at offset 8.");
TGL_STATIC_ASSERT(offsetof(TglProgramVTable, program_rez) == 16u, "TglProgramVTable::program_rez must sit at offset 16.");
TGL_STATIC_ASSERT(offsetof(TglProgramVTable, program_tick) == 24u, "TglProgramVTable::program_tick must sit at offset 24.");
TGL_STATIC_ASSERT(offsetof(TglProgramVTable, program_derez) == 32u, "TglProgramVTable::program_derez must sit at offset 32.");
TGL_STATIC_ASSERT(offsetof(TglProgramVTable, library_shutdown) == 40u, "TglProgramVTable::library_shutdown must sit at offset 40.");

#ifdef __cplusplus
}
#endif

#endif /* TGL_PROGRAM_ABI_H */
