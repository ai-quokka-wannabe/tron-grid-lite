# Program Interface

The contract between the Grid and a Program.

> **Status: pre-1.0, unstable.** Everything in this document may change without notice or
> deprecation period. There is no backwards-compatibility guarantee until the interface reaches
> version 1.0.0.

---

## Vocabulary

One word per concept, used consistently in this document and in the ABI itself.

| Word     | What it names                                                             |
|----------|---------------------------------------------------------------------------|
| the Grid | The world this project renders and simulates, and the binary that runs it |
| Program  | The thing that thinks: a shared library that drives one creature          |
| creature | The body the Grid simulates — geometry, eyes, actuators                   |
| User     | The human at the debug window, who watches and never acts                 |
| tick     | One simulation step                                                       |
| senses   | Everything a creature perceives during one tick (`TglSenses`)             |
| actions  | The physical intent a Program returns for that tick (`TglActions`)        |

**Program** and **User** are capitalised throughout: on the Grid they name kinds of being rather
than ordinary English words.

---

## Overview

The Grid is inhabited exclusively by Programs. There is no human player. The User may open a debug
window and fly a free camera through the Grid to observe and debug, but the User never acts, never
inhabits a body, and is never visible to any creature.

Every creature is driven by a **Program**: a shared library (`.dll` on Windows, `.so` on Linux)
built from a separate repository in the `ai-quokka-wannabe` organisation. The Grid loads Program
libraries at runtime, rezzes one Program onto each creature body, and exchanges data with it once
per tick.

The boundary between the Grid and a Program is deliberately narrow:

- A Program receives **raw senses** — chiefly a small block of samples rendered from the creature's
  own eyes, plus a handful of numbers about its own body. It receives no object identities, no
  positions, no orientations, no distances, no health bars, no scene graph, and no privileged Grid
  state of any kind.
- A Program emits **actions** — desired movement and turn rates. It cannot issue high-level commands
  such as "go to the blue tower" or "attack".
- A Program never links against the Grid binary and never calls Grid functions. All traffic crosses
  a plain **C ABI** of structs and function pointers.

What happens inside a Program — network, rule system, evolved controller, anything else — is the
Program author's business entirely. This document describes only the wire between them.

---

## Why the Interface Is This Narrow

A creature that can read object identities out of the Grid is not perceiving; it is being told.
The whole point of TronGrid Lite is that a Program must recover structure from raw senses, exactly
as an animal must. Any privileged channel would quietly remove the problem the project exists to
pose, so the interface simply does not have one.

The corollary is that the Grid owes a Program an honest sensory simulation, and nothing else.

---

## Plugin Model

### Discovery and loading

A Program is a single shared library. The Grid loads it with `LoadLibrary` (Windows) or `dlopen`
(Linux) and resolves exactly one exported symbol:

```c
/*! The single entry point every Program library must export with C linkage. */
const TglProgramVTable* tglGetProgramVTable(uint32_t requested_abi_version);
```

The Grid passes the ABI version it was built against. If the Program cannot satisfy that version it
returns `NULL`, the Grid logs the mismatch and refuses to run that Program. No negotiation, no
shims — a Program either speaks the current ABI or it does not load.

Everything else the Program needs is reached through the returned vtable, so a Program exports one
symbol and the Grid does exactly one symbol lookup.

### Lifecycle

```text
1. The Grid starts, reads its creature roster, resolves Program library paths.
2. dlopen / LoadLibrary the Program library.
3. tglGetProgramVTable(TGL_PROGRAM_ABI_VERSION) -> vtable, or NULL -> abort this Program.
4. vtable->library_init(&library_info)               once per loaded library
5. For each creature body assigned to this library:
       vtable->program_rez(&creature_desc)           -> opaque Program handle
6. Per tick, for each live creature:
       a. The Grid advances physics for the tick.
       b. The Grid renders each of that creature's eyes into its own tiny render target.
       c. The Grid reads the targets back and fills TglSenses, along with the body senses.
       d. vtable->program_tick(handle, &senses, &actions)
       e. The Grid clamps and applies TglActions to the body.
7. On creature death or Grid shutdown:
       vtable->program_derez(handle)
8. vtable->library_shutdown()                        once per loaded library
9. dlclose / FreeLibrary.
```

Steps 4 and 8 are called exactly once per library, regardless of how many creatures that library
drives. Steps 5 and 7 pair up strictly: every handle returned by `program_rez` receives exactly one
`program_derez`. A `program_rez` that returned `NULL` produced no handle and therefore never
receives a `program_derez` — the Grid skips that body and there is nothing to release.

---

## The C ABI Boundary

### Rules

- **Plain C only.** The header is compilable as C99. No C++ types, no exceptions, no RTTI, no
  templates, no `std::` anything crosses the boundary. A Program may be written in C++, Rust, Zig or
  anything else that can emit C-linkage exports, but the ABI itself is C.
- **No exceptions across the boundary.** A Program written in C++ must catch everything at its own
  export boundary. An exception unwinding into the Grid is undefined behaviour and will very likely
  terminate the process.
- **No memory ownership transfer.** Every pointer in a struct passed to a Program is owned by the
  Grid and is valid only for the duration of that call. The Program must copy anything it wishes to
  keep. Symmetrically, the Grid never frees anything a Program allocated.
- **One version number, checked once, and it does not move.** `TGL_PROGRAM_ABI_VERSION` catches a
  stale library; it does not express compatibility. There are no per-struct size fields and no
  compatibility shims — see [Versioning](#versioning).
- **Fixed-width types only.** `uint32_t`, `int32_t`, `float`, `uint64_t`. No `int`, no `long`, no
  `size_t`, no `bool`, no enums with unspecified underlying type, no bitfields.
- **Standard layout, natural padding.** Every struct here is a plain C standard-layout type of
  fixed-width members, so MSVC, GCC and Clang lay it out identically on both supported platforms.
  Members are grouped by modality for legibility rather than shuffled to eliminate the odd padding
  word, and no padding members are written by hand: the compiler's own padding is part of the ABI
  and both sides will be compiled from the same header once it ships.

### Vtable

```c
#define TGL_PROGRAM_ABI_VERSION 1u  /*!< Stays at 1 until 0.1.0. See Versioning. */

/*! Opaque per-creature Program state. The Grid never dereferences this. */
typedef struct TglProgram TglProgram;

/*! Function pointers a Program library provides to the Grid. */
typedef struct TglProgramVTable
{
    /* -- Library scope -------------------------------------------------------------- */

    /*! Called once after the library is loaded, before any Program is rezzed. */
    void (*library_init)(const TglLibraryInfo* info);

    /* -- Program scope -------------------------------------------------------------- */

    /*! Rezzes one Program onto one creature body. Returns NULL on failure; the Grid then skips
        that body, and a rez that returned NULL never receives a matching program_derez. */
    TglProgram* (*program_rez)(const TglCreatureDesc* desc);

    /*! Called once per tick per live creature. Must not block. */
    void (*program_tick)(TglProgram* program, const TglSenses* senses, TglActions* actions);

    /*! Derezzes one Program. The handle is invalid afterwards. */
    void (*program_derez)(TglProgram* program);

    /* -- Library scope -------------------------------------------------------------- */

    /*! Called once before the library is unloaded, after every Program has been derezzed. */
    void (*library_shutdown)(void);
} TglProgramVTable;
```

The whole interface is one exported symbol and five struct members, and the members fall into two
scopes: `library_init` and `library_shutdown` bracket the loaded library, while `program_rez`,
`program_tick` and `program_derez` bracket the life of one Program on one body. That split is what
the rest of this document rests on, so it is written into the struct rather than left implied.

The two library-scope members keep plain names on purpose. They are `LoadLibrary`/`dlopen` and
`FreeLibrary`/`dlclose`: facts about Windows and Linux rather than events on the Grid. Tron words
name events on the Grid; plain words name events in the operating system.

The rename that produced these names — `TglBrain` and `creature_create` and their siblings — changed
the exported symbol, both type names and three of the five members. No number records it, because
nothing external was built against the old ones. See [Versioning](#versioning).

### Library and creature descriptors

```c
/*! Grid-side facts a Program library may want at load time. */
typedef struct TglLibraryInfo
{
    uint32_t abi_version;    /*!< ABI version the Grid is running. */
    uint32_t tick_rate_hz;   /*!< Nominal ticks per second. */
} TglLibraryInfo;

/*! How one eye's samples are arranged. */
#define TGL_EYE_LAYOUT_RASTER      0u  /*!< A width x height grid, row-major, top row first. */
#define TGL_EYE_LAYOUT_SAMPLE_LIST 1u  /*!< Arbitrary sample directions; height is 1. */

/*! Geometry of one eye. Fixed for the creature's lifetime. */
typedef struct TglEyeDesc
{
    uint32_t layout;    /*!< One of TGL_EYE_LAYOUT_*. */

    /*! Raster width and height, or sample count and 1 for a sample list. */
    uint32_t width;
    uint32_t height;

    /*! Values per sample. 1 is a scalar; 3 is the usual three-band eye. What each band weights
        is a property of this body and is documented per sensor preset in PERCEPTION.md — it is
        emphatically not always RGB. */
    uint32_t channels;

    /*! For TGL_EYE_LAYOUT_SAMPLE_LIST, three floats per sample giving the unit view direction in
        body frame; NULL for a raster. A compound eye is a curved, non-uniform array rather than a
        rectangle, so the directions are handed over explicitly. Borrowed for the call only. */
    const float* sample_directions;

    /*! Field of view in radians. Meaningful for a raster; for a sample list the directions above
        are authoritative and these merely bound them. */
    float fov_horizontal;
    float fov_vertical;
} TglEyeDesc;

/*! Description of the body this Program will drive. Sensor geometry only — no Grid state. */
typedef struct TglCreatureDesc
{
    /*! Stable identifier for this creature within the run. Useful for the Program's own logging. */
    uint64_t creature_id;

    /*! Deterministic seed the Program should use for any randomness it needs. */
    uint64_t random_seed;

    /*! This body's eyes, in the order their views arrive each tick. May be zero — a creature with
        no image-forming eye at all is a legitimate body, and the simplest preset is exactly that.
        Borrowed for the duration of the call; copy anything worth keeping. */
    const TglEyeDesc* eyes;
    uint32_t eye_count;

    /*! Nominal seconds per tick. The actual value is repeated each tick in TglSenses. */
    float nominal_dt_seconds;
} TglCreatureDesc;
```

Note the direction of control: the **Grid** decides how many eyes a body has, how many samples each
one takes, in which directions and in how many channels, and hands the lot to the Program. A Program
does not request an eye. Sensor geometry is a property of the creature's body, not of its cognition,
and bodies are the Grid's business.

---

## Senses

A creature senses through zero or more small render targets, one per eye, each rendered by the same
ray-tracing compute path that renders the User's window, from that eye's own position. Sizes,
channel counts and layouts vary by preset, and some eyes are sample-direction lists rather than
rasters. They are deliberately tiny — a few samples to a few tens of thousands — because animal
eyes are also modest in the terms that matter here, and because a creature must be cheap enough to
simulate in numbers. See `docs/PERCEPTION.md` for the reasoning behind the sizes and the sensor
model.

```c
/*! One eye's samples for this tick. */
typedef struct TglEyeView
{
    /*! Linear-space sample values, `channels` floats per sample, in the order described by the
        matching TglEyeDesc. Never NULL. */
    const float* samples;

    /*! Number of samples, not floats. Equals width * height of the matching TglEyeDesc. */
    uint32_t sample_count;

    /*! Values per sample, repeated from TglEyeDesc so a tick handler needs nothing else. */
    uint32_t channels;
} TglEyeView;

/*! Everything a creature perceives this tick. All pointers are borrowed for the call only. */
typedef struct TglSenses
{
    /*! Monotonic tick counter. */
    uint64_t tick;

    /*! Duration of this tick in seconds. */
    float dt_seconds;

    /* -- Vision ------------------------------------------------------------------- */

    /*! One view per eye, in the same order as TglCreatureDesc::eyes. NULL when eye_count is 0. */
    const TglEyeView* eyes;
    uint32_t eye_count;

    /* -- Proprioception ----------------------------------------------------------- */

    /*! What the body's own actuators report about themselves. */
    float body_forward_speed;  /*!< Metres per second along the body axis. */
    float body_turn_rate;      /*!< Radians per second about the body's up axis. */

    /* -- Vestibular ---------------------------------------------------------------- */

    /*! Specific force in body frame, in metres per second squared: linear acceleration with
        gravity included, exactly what an otolith or an accelerometer senses. A creature at rest
        reads roughly 9.81 along its up axis. Gravity and acceleration are deliberately conflated
        in one number, because they are conflated in the animal too — which is precisely why a
        creature can be fooled about which way is down. On a Grid of perfect mirrors it will be. */
    float specific_force[3];

    /*! Angular velocity about the body axes in radians per second — the semicircular-canal
        analogue. Sensed inertially, which is not the same thing as the commanded turn rate above:
        the two disagree whenever the body is pushed rather than driven. */
    float angular_velocity[3];

    /* -- Touch -------------------------------------------------------------------- */

    /*! Contact intensity from collisions this tick, normalised to [0, 1]. */
    float touch_front;
    float touch_rear;
    float touch_lateral;

    /* -- Thermoreception ------------------------------------------------------------ */

    /*! Irradiance at the creature's position in the renderer's linear units: incoming radiance
        integrated over the whole sphere, with no directional resolution whatsoever. One number,
        one extra ray. This is not a cheapened eye but a faithful one — a pit viper's infrared
        organ is likewise a very low-resolution radiance sensor, and it is enough to tell warm
        from cold long before it is enough to see. */
    float irradiance;
} TglSenses;
```

### The senses at a glance

| Sense | Field | What it physically is | Status |
|-------|-------|----------------------|--------|
| Vision | `eyes` | Rays traced from each eye through the shared BVH | Phase 6 |
| Proprioception | `body_forward_speed`, `body_turn_rate` | The body's own actuator state | Phase 6 |
| Vestibular | `specific_force`, `angular_velocity` | Derivatives the motion integrator already has | Phase 6 |
| Touch | `touch_*` | Short contact queries against the same BVH | Phase 6 |
| Thermoreception | `irradiance` | One unresolved radiance sample | Phase 6 |
| Hearing | `ears` | Acoustic rays through the same BVH, delivered as an impulse response per ear | Phase 6 |

Every one of them is a **consequence of the creature's position on the Grid**, computed by the
same machinery that draws the picture. That is the test a modality has to pass to belong here.

Points worth stating plainly, because they are the whole design:

- An eye delivers **samples**. It is not a list of what is in front of the creature. Working out
  that a bright cyan band means a neon edge, and that a neon edge means a wall, is the Program's
  job.
- Proprioception, vestibular sensing and touch describe the creature's **own body**, which an
  animal genuinely does have access to. They describe nothing outside it.
- There is deliberately no field for position, heading in Grid coordinates, distance to anything,
  object identifiers, or other creatures' states. No such field will be added. A compass was
  considered and rejected for exactly this reason: it would hand the Program the structure the Grid
  exists to make it earn.
- Nothing here is a valuation. The Grid reports contact intensity; whether that amounts to pain is
  the Program's business, and pain is cognition wearing a sensory costume.
- Every field is populated. A modality that does not exist yet has no field waiting for it — the
  struct describes what the Grid can currently sense, and gains members when that changes.
- **An ear delivers an impulse response, not a sound.** It says how much energy arrived in each
  frequency band at each delay. Working out that two arrivals four milliseconds apart mean a surface
  seventy centimetres away is the Program's job, exactly as it is the Program's job to decide that a
  bright cyan band is a wall.

### Hearing

Acoustic sensing is **built**. The same hand-built BVH that answers visual rays answers acoustic ones,
through the same traversal module, so hearing arrives as sample buffers rather than as a channel
bolted on elsewhere. What a Program receives is an **impulse response per ear**: energy against delay,
resolved into that ear's own frequency bands.

The prerequisite that used to be open is closed. The Grid now has a sound source, and it is the neon:
light and sound come from the very same geometry, which is "one Grid, two senses" earning its keep
rather than being asserted. Surfaces carry one authored acoustic number — how loudly they sing — and
nothing else, because acoustically every surface on the Grid is a perfect mirror. See
[ACOUSTICS.md](ACOUSTICS.md) for why, and for the open-half-space assumption that makes it safe.

```c
/*! Geometry of one ear. Fixed for the creature's lifetime. */
typedef struct TglEarDesc
{
    float position[3];        /*!< Ear position in body frame, metres. */
    float direction[3];       /*!< Ear axis in body frame, unit length. */

    /*! Band edges in hertz, band_count + 1 values, ascending. Borrowed for the call only.
        Chosen from this body's audiogram, not from room-acoustics convention. */
    const float* band_edges_hz;
    uint32_t band_count;

    uint32_t bin_count;       /*!< Time bins delivered per tick. */
    float bin_seconds;        /*!< Width of one bin. */

    /*! Atmospheric absorption per band, decibels per kilometre, band_count values.
        Borrowed for the call only. Authored beside the band edges because both follow from the
        same audiogram: a creature that hears in a different place needs different numbers in
        both. There is deliberately no function on the Grid that turns a frequency into an
        absorption — the Grid is fixed at 20 C and 70 % humidity, so these are constants. */
    const float* air_absorption_db_per_km;
} TglEarDesc;

/*! One ear's arrivals for this tick. */
typedef struct TglEarView
{
    /*! Energy per (bin, band), bin-major, bin_count * band_count floats. Never NULL.
        Relative to the emitting source's energy; there is no absolute reference level.
        A bin no sound has reached yet reads zero, which is the physical answer and not a
        sentinel: there is no "not yet filled" state to flag. */
    const float* energy;

    uint32_t bin_count;       /*!< Repeated from TglEarDesc so a tick handler needs nothing else. */
    uint32_t band_count;
} TglEarView;
```

`TglCreatureDesc` gains `const TglEarDesc* ears; uint32_t ear_count;`, and `TglSenses` gains
`const TglEarView* ears; uint32_t ear_count;` under the `/* -- Hearing -- */` banner, keeping the
modality grouping this document commits to. An `ear_count` of zero is a legitimate body, and is the
correct specification for all three insect presets.

The direction of control is the same as for eyes: **the Grid decides how many ears a body has, where
they sit, and what bands they resolve.** A Program does not request an ear.

#### Nothing on the Grid sounds continuously

Every source is pulsed, one-shot or modulated: the neon pulses rather than holding a tone, a
vocalisation is a call that stops, and a body dragging along the floor scrapes — sustained, but
modulated by its own gait rather than held at a level.

This matters to a Program rather than only to the Grid, because **a continuous tone would carry almost
no delay information.** Every arrival would overlap every other and the ear would receive a steady
level with nothing to measure, which would make the millisecond bins above describe something no
Program could extract. Onsets are what make a delay measurable, which is why bats pulse rather than
hum, and it is why the response is worth delivering at this resolution at all.

One consequence is worth stating because it is physically correct and was not designed in: a sustained
noisy source is **easy to detect and hard to range**. A Program that expects every sound to be
localisable will be wrong about the quiet ones, and that is the Grid being honest rather than
incomplete.

#### Echolocation, and the scope line

**Echolocation needs no new sense.** A creature that can emit a sound and hear the reflections has it
already, which is why the vocalisation action and the hearing sense arrive together rather than in
two separate breaking changes — see [Actions](#actions).

One scope caution, because it is easy to violate by accident. The ABI delivers **energy per band per
time bin per ear, and stops.** Anything that names a source, separates streams, reports "a wall is
three metres to your left", or performs auditory scene analysis of any kind belongs in a Program
repository. The Grid makes localisation *possible* by delivering two ears with their own signals; it
does not perform it.

### Chemoreception: deliberately absent, and worth revisiting

Smell has no place on the Grid as it stands because there is no chemistry in it — only geometry,
light and, soon, sound. Adding it would mean a diffusing scalar field, the first subsystem the BVH
cannot help with at all.

It is recorded here rather than dismissed because there is a real argument on the other side. The
simplest sensor preset is named after an animal that navigates chiefly by chemotaxis, not by light,
so the simplest possible creature is arguably specified with the wrong sense — and gradient
following would be a far gentler first problem for a Program than interpreting an image.

---

## Actions

An action is physical intent, expressed in the body frame, and nothing more.

```c
/*! What the creature attempts to do this tick. The Grid zeroes this before each call. */
typedef struct TglActions
{
    /*! Desired forward speed in metres per second. Negative reverses. Clamped by the Grid. */
    float desired_forward_speed;

    /*! Desired turn rate about the body's up axis in radians per second.
        Positive is anticlockwise seen from above. Clamped by the Grid. */
    float desired_turn_rate;

    /*! Desired vertical speed in metres per second, body frame. Clamped by the Grid. */
    float desired_vertical_speed;

    /* -- Vocalisation -- */

    /*! Loudness of a call emitted this tick, relative to a primary neon tube. Zero is silent.
        Clamped by the Grid to what this body can produce.

        A **call**, not a channel: the Grid emits it as a single burst from the creature's own
        position and it is over. There is no field for its duration, spectrum or waveform, because
        there is no waveform anywhere on the Grid — the body's descriptor carries what its voice
        sounds like, and this says only whether it used it and how hard. Setting it every tick
        produces a train of calls, which is what an echolocating animal does; it does not produce a
        continuous tone, and nothing on the Grid can. */
    float vocalisation_strength;
} TglActions;
```

The Grid **clamps** every field to the body's physical limits and then feeds the result to the
physics step as intent, not as a teleport. A Program asking for one hundred metres per second gets
whatever its body can actually manage. Non-finite values (`NaN`, infinities) are treated as zero
and logged.

There are no fields for "interact with object X", "pick up" or "attack", and adding them would defeat
the purpose. If a creature is to do something to the Grid, it must do it by moving a body through
space — or, now, by making a noise in it.

**Vocalisation is the one exception, and it earns the exception by being physical.** A call is a
pressure wave leaving a position, propagating through the same geometry every other sound does, and
arriving at ears that are subject to the same delays and the same inverse square law. It is not
speech, it carries no message and it addresses nobody: it is the acoustic counterpart of a body
occupying space. It is deliberately *not* "speak", and a Program that wants to signal another Program
must do it the way animals do — by making a noise whose meaning the listener has to learn.

It arrives together with hearing rather than after it, because **echolocation needs no new sense**:
a creature that can emit a sound and hear the reflections already has it, so splitting the two into
separate breaking changes would have been a change that did nothing on its own.

---

## Threading and Ownership

- `program_tick` is called from the **tick thread**. Which OS thread that is may change between
  ticks; it will not change during one.
- The Grid may tick **different creatures in parallel**. A Program library must therefore treat
  `program_tick` as reentrant across distinct handles: per-creature state is fine, shared mutable
  library state must be synchronised by the Program, and any global mutable state is a bug waiting
  to happen.
- `library_init`, `program_rez`, `program_derez` and `library_shutdown` are called from a single
  thread with no other Program call in flight.
- `program_tick` **must not block**. It runs inside the tick budget. A Program that needs
  long-running work must do it on its own thread and let `program_tick` read the most recent result.
- A Program may create its own threads, windows and files. The Grid neither knows nor cares. It
  provides no logging facility and no debug display to Programs; a Program that wants to visualise
  its own state opens its own window.
- Lifetimes: `senses` and every pointer inside it are valid only for the duration of the
  `program_tick` call. `actions` is valid for the same window and is the only memory a Program may
  write to across the boundary.

### Crash behaviour

Programs run **in process**. There is no sandbox and no crash isolation. If a Program segfaults, the
Grid dies with it. This is a deliberate simplification for a project whose Programs are written by
the same small group as the Grid; it may be revisited if that ever stops being true.

### Hot reload

Not supported. Restart the Grid to load a different Program build.

---

## Determinism and Replay

The interface is designed so that a run can be reproduced:

- Each creature receives a `random_seed` when its Program is rezzed rather than seeding itself from
  the clock.
- The Grid's ray tracing is Whitted-style and deterministic: three surface kinds (mirror, emissive
  and simple glass), no Monte Carlo sampling, no temporal accumulation, no denoiser. The same
  camera pose on the same Grid yields bit-identical pixels.
- `dt_seconds` is supplied explicitly rather than measured by the Program.

Consequently, recording every `TglSenses` a Program saw and replaying the sequence into the same
Program build must reproduce the same `TglActions` sequence. A Program that fails this is
non-deterministic internally, which is allowed but should be a deliberate choice.

---

## Versioning

**There is none, and that is deliberate until 0.1.0.**

`TGL_PROGRAM_ABI_VERSION` is `1u` and stays at `1u`. This interface changes whenever it needs to —
structs gain fields, functions change signature, semantics get corrected — and none of that bumps
the number, because nobody is owed backward compatibility by a project that has not reached its
first release. Every Program that exists is built from this repository, at whatever commit the Grid
is built from. When the ABI changes, both sides rebuild. That is the whole story.

The constant is kept for the one job it can still do honestly: catching a **stale library**. A
`.dll` or `.so` left over from an older build, loaded against a Grid whose struct layouts have moved
underneath it, is memory corruption with no diagnostic. One integer compared at load time turns that
into a refusal and a log line.

- `tglGetProgramVTable` returns `NULL` if the requested version is not `1u`. The Grid refuses to
  load that Program and says so. That single check is the entire mechanism.
- The Grid's own release version is unrelated and moves independently.

Real versioning arrives when there is something to version — a released Grid, and Programs written
by somebody who cannot simply rebuild them. Until then a rising number would record ceremony rather
than compatibility, and it has already drifted once: the roadmap asked for a bump that a rename had
silently already spent.

There is deliberately nothing else — no per-struct size fields, no reserved members held back for
future growth, no negotiation. Those exist to let mismatched builds keep working, and while the
interface has no users that is machinery bought at the price of clutter in every struct. Whatever
policy replaces this at 1.0.0 can be designed then, against a real interface rather than a guess.

Practical advice while the interface is pre-1.0: rebuild your Program whenever the Grid is rebuilt.

---

## Writing a Program

**Status: `tgl_program_abi.h` does not exist yet — it lands with Phase 6. Everything declared above
is the design, not a shipped API, and it may still change before the header is written.**

1. Transcribe the vtable and the structs from this document until the header ships. It is intended
   to be a single self-contained C header with no dependency on any Grid internals.
2. Implement the five vtable functions and export `tglGetProgramVTable`.
3. Build as a shared library with C-linkage exports, in whichever language you prefer.
4. Point the Grid's creature roster at the resulting `.dll` or `.so`.

Nothing else is required, and nothing else is offered.

---

*See also: `docs/PERCEPTION.md` for the sensor model, `docs/DEV_ENV_SETUP.md` for building the
Grid, and `STYLE.md` for code style.*
