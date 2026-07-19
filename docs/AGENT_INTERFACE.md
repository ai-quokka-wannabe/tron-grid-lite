# Agent Interface

The contract between the TronGrid Lite world and an AI creature brain.

> **Status: pre-1.0, unstable.** Everything in this document may change without notice or
> deprecation period. There is no backwards-compatibility guarantee until the interface reaches
> version 1.0.0.

---

## Overview

TronGrid Lite is a world inhabited exclusively by AI creature agents. There is no human player.
A human may open a spectator window and fly a free camera through the world to observe and debug,
but the spectator never acts, never inhabits a body, and is never visible to any creature.

Every creature is driven by a **brain**: a shared library (`.dll` on Windows, `.so` on Linux)
built from a separate repository in the `ai-quokka-wannabe` organisation. The world loads brains
at runtime, gives each creature body a brain instance, and exchanges data with it once per
simulation tick.

The boundary between world and brain is deliberately narrow:

- The brain receives **raw sensor buffers** — chiefly a small block of pixels rendered from the
  creature's own eye. It receives no object identities, no positions, no orientations, no
  distances, no health bars, no scene graph, and no privileged world state of any kind.
- The brain emits **physical motor intent** — desired movement and turn rates. It cannot issue
  high-level commands such as "go to the blue tower" or "attack".
- The brain never links against the world binary and never calls world functions. All traffic
  crosses a plain **C ABI** of structs and function pointers.

What happens inside a brain — network, rule system, evolved controller, anything else — is the
brain author's business entirely. This document describes only the wire between them.

---

## Why the Interface Is This Narrow

A creature that can read object identities out of the world is not perceiving; it is being told.
The whole point of TronGrid Lite is that a brain must recover structure from raw sensory data,
exactly as an animal must. Any privileged channel would quietly remove the problem the project
exists to pose, so the interface simply does not have one.

The corollary is that the world owes the brain an honest sensory simulation, and nothing else.

---

## Plugin Model

### Discovery and loading

A brain is a single shared library. The world loads it with `LoadLibrary` (Windows) or `dlopen`
(Linux) and resolves exactly one exported symbol:

```c
/*! The single entry point every brain library must export with C linkage. */
const TglBrainVTable* tglGetBrainVTable(uint32_t requested_abi_version);
```

The world passes the ABI version it was built against. If the brain cannot satisfy that version it
returns `NULL`, the world logs the mismatch and refuses to run that brain. No negotiation, no
shims — a brain either speaks the current ABI or it does not load.

Everything else the brain needs is reached through the returned vtable, so the brain exports one
symbol and the world does exactly one symbol lookup.

### Lifecycle

```text
1. World starts, reads its creature roster, resolves brain library paths.
2. dlopen / LoadLibrary the brain library.
3. tglGetBrainVTable(TGL_BRAIN_ABI_VERSION) -> vtable, or NULL -> abort this brain.
4. vtable->library_init(&library_info)               once per loaded library
5. For each creature body assigned to this library:
       vtable->creature_create(&creature_desc)       -> opaque brain handle
6. Per simulation tick, for each live creature:
       a. World advances physics for the tick.
       b. World renders that creature's sensor view into its own tiny render target.
       c. World reads the target back and fills TglSenses.
       d. vtable->creature_tick(handle, &senses, &actions)
       e. World clamps and applies TglActions to the body.
7. On creature death or world shutdown:
       vtable->creature_destroy(handle)
8. vtable->library_shutdown()                        once per loaded library
9. dlclose / FreeLibrary.
```

Steps 4 and 8 are called exactly once per library, regardless of how many creatures that library
drives. Steps 5 and 7 pair up strictly: every handle returned by `creature_create` receives
exactly one `creature_destroy`.

---

## The C ABI Boundary

### Rules

- **Plain C only.** The header is compilable as C99. No C++ types, no exceptions, no RTTI, no
  templates, no `std::` anything crosses the boundary. A brain may be written in C++, Rust, Zig or
  anything else that can emit C-linkage exports, but the ABI itself is C.
- **No exceptions across the boundary.** A brain written in C++ must catch everything at its own
  export boundary. An exception unwinding into the world is undefined behaviour and will very
  likely terminate the process.
- **No memory ownership transfer.** Every pointer in a struct passed to the brain is owned by the
  world and is valid only for the duration of that call. The brain must copy anything it wishes to
  keep. Symmetrically, the world never frees anything a brain allocated.
- **Explicit versioning.** Every struct that crosses the boundary carries a `struct_size` field as
  its first member, and the whole ABI carries a monotonic `TGL_BRAIN_ABI_VERSION`. See
  [Versioning](#versioning).
- **Fixed-width types only.** `uint32_t`, `int32_t`, `float`, `uint64_t`. No `int`, no `long`, no
  `size_t`, no `bool`, no enums with unspecified underlying type, no bitfields.
- **Standard layout, explicit padding.** All structs are declared so that MSVC, GCC and Clang agree
  on the layout on both supported platforms. Padding is written out as named reserved members
  rather than left implicit.

### Vtable

```c
#define TGL_BRAIN_ABI_VERSION 0u  /*!< Bumped on every breaking change until 1.0. */

/*! Opaque per-creature brain state. The world never dereferences this. */
typedef struct TglBrain TglBrain;

/*! Function pointers a brain library provides to the world. */
typedef struct TglBrainVTable
{
    uint32_t struct_size;  /*!< Must equal sizeof(TglBrainVTable). */
    uint32_t abi_version;  /*!< Must equal TGL_BRAIN_ABI_VERSION. */

    /*! Called once after the library is loaded, before any creature is created. */
    void (*library_init)(const TglLibraryInfo* info);

    /*! Creates one creature brain. Returns NULL on failure; the world then skips that body. */
    TglBrain* (*creature_create)(const TglCreatureDesc* desc);

    /*! Called once per simulation tick per live creature. Must not block. */
    void (*creature_tick)(TglBrain* brain, const TglSenses* senses, TglActions* actions);

    /*! Destroys one creature brain. The handle is invalid afterwards. */
    void (*creature_destroy)(TglBrain* brain);

    /*! Called once before the library is unloaded, after every creature has been destroyed. */
    void (*library_shutdown)(void);
} TglBrainVTable;
```

### Library and creature descriptors

```c
/*! World-side facts a brain library may want at load time. */
typedef struct TglLibraryInfo
{
    uint32_t struct_size;
    uint32_t abi_version;    /*!< ABI version the world is running. */
    uint32_t tick_rate_hz;   /*!< Nominal simulation ticks per second. */
    uint32_t reserved0;
} TglLibraryInfo;

/*! Description of the body this brain will drive. Sensor geometry only — no world state. */
typedef struct TglCreatureDesc
{
    uint32_t struct_size;
    uint32_t reserved0;    /*!< Explicit padding before the 8-byte-aligned members below. */

    /*! Stable identifier for this creature within the run. Useful for the brain's own logging. */
    uint64_t creature_id;

    /*! Deterministic seed the brain should use for any randomness it needs. */
    uint64_t random_seed;

    /*! Sensor render target dimensions in pixels, decided by the world (see PERCEPTION.md). */
    uint32_t eye_width;
    uint32_t eye_height;

    /*! Horizontal field of view of the eye, in radians. */
    float eye_fov_horizontal;

    /*! Nominal seconds per tick. The actual value is repeated each tick in TglSenses. */
    float nominal_dt_seconds;
} TglCreatureDesc;
```

Note the direction of control: the **world** decides the sensor resolution and hands it to the
brain. A brain does not request an eye. Sensor geometry is a property of the creature's body, not
of its cognition, and bodies are the world's business.

---

## Sensory Input

A creature senses through one small render target of its own, rendered by the same ray-tracing
compute path that renders the spectator window, from the creature's own eye position, at the
creature's own resolution. Typical sizes are 64x64 to 256x256 — deliberately tiny, because animal
eyes are also modest in the terms that matter here, and because a creature must be cheap enough to
simulate in numbers. See `docs/PERCEPTION.md` for the reasoning behind the sizes and the sensor
model.

```c
/*! Everything a creature perceives this tick. All pointers are borrowed for the call only. */
typedef struct TglSenses
{
    uint32_t struct_size;
    uint32_t reserved0;

    /*! Monotonic simulation tick counter. */
    uint64_t tick;

    /*! Duration of this tick in seconds. */
    float dt_seconds;
    uint32_t reserved1;

    /* -- Vision ------------------------------------------------------------------- */

    /*! Linear-space RGB pixels from this creature's eye, row-major, top row first.
        Length is eye_width * eye_height * 3 floats. Never NULL. */
    const float* vision_rgb;
    uint32_t vision_width;
    uint32_t vision_height;

    /* -- Proprioception ----------------------------------------------------------- */

    /*! Forward speed along the body axis, in metres per second, body frame. */
    float body_forward_speed;
    /*! Turn rate about the body's up axis, in radians per second, body frame. */
    float body_turn_rate;

    /* -- Touch -------------------------------------------------------------------- */

    /*! Contact intensity from collisions this tick, normalised to [0, 1]. */
    float touch_front;
    float touch_rear;
    float touch_lateral;
    uint32_t reserved2;  /*!< Explicit padding before the 8-byte-aligned pointer below. */

    /* -- Reserved for future modalities -------------------------------------------- */

    /*! Hearing. Populated once acoustic rays share the BVH; NULL and zero until then. */
    const float* hearing_samples;
    uint32_t hearing_sample_count;
    uint32_t reserved3;
} TglSenses;
```

Points worth stating plainly, because they are the whole design:

- `vision_rgb` is **pixels**. It is not a list of what is in front of the creature. Working out
  that a bright cyan band means a neon edge, and that a neon edge means a wall, is the brain's job.
- Proprioception and touch describe the creature's **own body**, which an animal genuinely does
  have access to. They describe nothing outside it.
- There is deliberately no field for position, heading in world coordinates, distance to anything,
  object identifiers, or other creatures' states. No such field will be added.
- Reserved members are present so the struct can grow without changing its size in a way that
  silently breaks older brains. They are zero until they mean something.

Acoustic sensing is the next modality planned. Surfaces in TronGrid Lite already carry acoustic
properties alongside their optical ones, and the same hand-built BVH is intended to serve acoustic
rays, so hearing will arrive as sample buffers filled through `hearing_samples` rather than as a
new channel bolted on elsewhere.

---

## Motor Output

Motor output is physical intent, expressed in the body frame, and nothing more.

```c
/*! What the creature attempts to do this tick. The world zeroes this before each call. */
typedef struct TglActions
{
    uint32_t struct_size;
    uint32_t reserved0;

    /*! Desired forward speed in metres per second. Negative reverses. Clamped by the world. */
    float desired_forward_speed;

    /*! Desired turn rate about the body's up axis in radians per second.
        Positive is anticlockwise seen from above. Clamped by the world. */
    float desired_turn_rate;

    /*! Desired vertical speed in metres per second, body frame. Clamped by the world. */
    float desired_vertical_speed;

    float reserved1;
} TglActions;
```

The world **clamps** every field to the body's physical limits and then feeds the result to the
physics step as intent, not as a teleport. A brain asking for one hundred metres per second gets
whatever its body can actually manage. Non-finite values (`NaN`, infinities) are treated as zero
and logged.

There are no fields for "interact with object X", "pick up", "attack", or "speak", and adding them
would defeat the purpose. If a creature is to do something to the world, it must do it by moving a
body through space.

---

## Threading and Ownership

- `creature_tick` is called from the **simulation thread**. Which OS thread that is may change
  between ticks; it will not change during one.
- The world may tick **different creatures in parallel**. A brain library must therefore treat
  `creature_tick` as reentrant across distinct handles: per-creature state is fine, shared mutable
  library state must be synchronised by the brain, and any global mutable state is a bug waiting to
  happen.
- `library_init`, `creature_create`, `creature_destroy` and `library_shutdown` are called from a
  single thread with no other brain call in flight.
- `creature_tick` **must not block**. It runs inside the simulation's frame budget. A brain that
  needs long-running work must do it on its own thread and let `creature_tick` read the most recent
  result.
- A brain may create its own threads, windows and files. The world neither knows nor cares. It
  provides no logging facility and no debug display to brains; a brain that wants to visualise its
  own state opens its own window.
- Lifetimes: `senses` and every pointer inside it are valid only for the duration of the
  `creature_tick` call. `actions` is valid for the same window and is the only memory a brain may
  write to across the boundary.

### Crash behaviour

Brains run **in process**. There is no sandbox and no crash isolation. If a brain segfaults, the
world dies with it. This is a deliberate simplification for a project whose brains are written by
the same small group as the world; it may be revisited if that ever stops being true.

### Hot reload

Not supported. Restart the world to load a different brain build.

---

## Determinism and Replay

The interface is designed so that a run can be reproduced:

- Each creature receives a `random_seed` at creation rather than seeding itself from the clock.
- The world's ray tracing is Whitted-style and deterministic: three surface kinds (mirror, emissive
  and simple glass), no Monte Carlo sampling, no temporal accumulation, no denoiser. The same
  camera pose in the same world yields bit-identical pixels.
- `dt_seconds` is supplied explicitly rather than measured by the brain.

Consequently, recording every `TglSenses` a brain saw and replaying the sequence into the same
brain build must reproduce the same `TglActions` sequence. A brain that fails this is
non-deterministic internally, which is allowed but should be a deliberate choice.

---

## Versioning

Until 1.0.0 the rule is simple: **breaking changes only, no compatibility shims.**

- `TGL_BRAIN_ABI_VERSION` increases by one on every change to any struct layout, function signature
  or semantic contract in this document.
- `tglGetBrainVTable` returns `NULL` if it cannot serve the requested version. The world then
  refuses to load that brain and says so.
- Every boundary struct's first member is `struct_size`. Both sides check it. A mismatch is a hard
  error, not something to paper over.
- The world's own release version and the ABI version are independent. Only the ABI version governs
  brain compatibility.

After 1.0.0 the intention is: additive changes bump a minor version and remain loadable by older
brains through the `struct_size` mechanism; layout or semantic changes bump the major version and
do not. That policy is a statement of intent, not yet a promise.

Practical advice while the interface is pre-1.0: rebuild your brain whenever the world is rebuilt.

---

## Writing a Brain

1. Take `tgl_brain_abi.h` from the world repository. It is a single self-contained C header with no
   dependency on any world internals.
2. Implement the five vtable functions and export `tglGetBrainVTable`.
3. Build as a shared library with C-linkage exports, in whichever language you prefer.
4. Point the world's creature roster at the resulting `.dll` or `.so`.

Nothing else is required, and nothing else is offered.

---

*See also: `docs/PERCEPTION.md` for the sensor model, `docs/DEV_ENV_SETUP.md` for building the
world, and `STYLE.md` for code style.*
