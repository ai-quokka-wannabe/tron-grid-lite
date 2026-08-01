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

- The brain receives **raw sensor buffers** — chiefly a small block of samples rendered from the
  creature's own eyes, plus a handful of numbers about its own body. It receives no object
  identities, no positions, no orientations, no distances, no health bars, no scene graph, and no
  privileged world state of any kind.
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
       b. World renders each of that creature's eyes into its own tiny render target.
       c. World reads the targets back and fills TglSenses, along with the body senses.
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
- **One version number, checked once.** `TGL_BRAIN_ABI_VERSION` is the whole versioning mechanism.
  There are no per-struct size fields and no compatibility shims — see [Versioning](#versioning).
- **Fixed-width types only.** `uint32_t`, `int32_t`, `float`, `uint64_t`. No `int`, no `long`, no
  `size_t`, no `bool`, no enums with unspecified underlying type, no bitfields.
- **Standard layout, natural padding.** Every struct here is a plain C standard-layout type of
  fixed-width members, so MSVC, GCC and Clang lay it out identically on both supported platforms.
  Members are grouped by modality for legibility rather than shuffled to eliminate the odd padding
  word, and no padding members are written by hand: the compiler's own padding is part of the ABI
  and both sides are compiled from the same header.

### Vtable

```c
#define TGL_BRAIN_ABI_VERSION 1u  /*!< Bumped on every breaking change until 1.0. */

/*! Opaque per-creature brain state. The world never dereferences this. */
typedef struct TglBrain TglBrain;

/*! Function pointers a brain library provides to the world. */
typedef struct TglBrainVTable
{
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
    uint32_t abi_version;    /*!< ABI version the world is running. */
    uint32_t tick_rate_hz;   /*!< Nominal simulation ticks per second. */
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

/*! Description of the body this brain will drive. Sensor geometry only — no world state. */
typedef struct TglCreatureDesc
{
    /*! Stable identifier for this creature within the run. Useful for the brain's own logging. */
    uint64_t creature_id;

    /*! Deterministic seed the brain should use for any randomness it needs. */
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

Note the direction of control: the **world** decides how many eyes a body has, how many samples
each one takes, in which directions and in how many channels, and hands the lot to the brain. A
brain does not request an eye. Sensor geometry is a property of the creature's body, not of its
cognition, and bodies are the world's business.

---

## Sensory Input

A creature senses through one small render target of its own, rendered by the same ray-tracing
compute path that renders the spectator window, from the creature's own eye position, at the
creature's own resolution. Typical sizes are 64x64 to 256x256 — deliberately tiny, because animal
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
    /*! Monotonic simulation tick counter. */
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
        creature can be fooled about which way is down. In a world of perfect mirrors it will be. */
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
| Hearing | not yet in the struct | Acoustic rays through the same BVH | Phase 5 onwards |

Every one of them is a **consequence of the creature's position in the world**, computed by the
same machinery that draws the picture. That is the test a modality has to pass to belong here.

Points worth stating plainly, because they are the whole design:

- An eye delivers **samples**. It is not a list of what is in front of the creature. Working out
  that a bright cyan band means a neon edge, and that a neon edge means a wall, is the brain's job.
- Proprioception, vestibular sensing and touch describe the creature's **own body**, which an
  animal genuinely does have access to. They describe nothing outside it.
- There is deliberately no field for position, heading in world coordinates, distance to anything,
  object identifiers, or other creatures' states. No such field will be added. A compass was
  considered and rejected for exactly this reason: it would hand the brain the structure the world
  exists to make it earn.
- Nothing here is a valuation. The world reports contact intensity; whether that amounts to pain is
  the brain's business, and pain is cognition wearing a sensory costume.
- Every field is populated. A modality that does not exist yet has no field waiting for it — the
  struct describes what the world can currently sense, and gains members when that changes.

### Hearing, and the thing it still needs

Acoustic sensing is the next modality planned. The same hand-built BVH is intended to serve acoustic
rays, so hearing will arrive as sample buffers rather than as a channel bolted on elsewhere.

Surfaces do **not** yet carry acoustic properties: the fields were reserved once, removed as bloat
because nothing read them, and will return in Phase 5 when something does. See
[ACOUSTICS.md](ACOUSTICS.md).

One prerequisite is still open, and it is not a technical one: **nothing in this world currently
makes a sound.** Surfaces emit light; none of them emit anything audible. Before a hearing field
can carry meaning the world needs sources, and there are two natural candidates — the creatures
themselves, through movement and collision, and the neon itself, humming as gas-discharge tubes do.
The second is appealing because it would make the world's light and its sound come from the very
same geometry.

A pleasant consequence follows once hearing exists: **echolocation needs no new sense at all.** A
creature that can emit a sound and hear the reflections has it already, so the decision to add a
vocalisation action belongs with the decision to add hearing.

### Chemoreception: deliberately absent, and worth revisiting

Smell has no place in the current world because there is no chemistry in it — only geometry, light
and, soon, sound. Adding it would mean a diffusing scalar field, the first subsystem the BVH cannot
help with at all.

It is recorded here rather than dismissed because there is a real argument on the other side. The
simplest sensor preset is named after an animal that navigates chiefly by chemotaxis, not by light,
so the simplest possible creature is arguably specified with the wrong sense — and gradient
following would be a far gentler first problem for a brain than interpreting an image.

---

## Motor Output

Motor output is physical intent, expressed in the body frame, and nothing more.

```c
/*! What the creature attempts to do this tick. The world zeroes this before each call. */
typedef struct TglActions
{
    /*! Desired forward speed in metres per second. Negative reverses. Clamped by the world. */
    float desired_forward_speed;

    /*! Desired turn rate about the body's up axis in radians per second.
        Positive is anticlockwise seen from above. Clamped by the world. */
    float desired_turn_rate;

    /*! Desired vertical speed in metres per second, body frame. Clamped by the world. */
    float desired_vertical_speed;
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

Until 1.0.0 the rule is simple: **breaking changes only, no compatibility machinery.**

- `TGL_BRAIN_ABI_VERSION` increases by one on every change to any struct layout, function signature
  or semantic contract in this document.
- `tglGetBrainVTable` returns `NULL` if it cannot serve the requested version. The world then
  refuses to load that brain and says so. That single check is the entire mechanism.
- The world's own release version and the ABI version are independent. Only the ABI version governs
  brain compatibility.

There is deliberately nothing else — no per-struct size fields, no reserved members held back for
future growth, no negotiation. Those exist to let mismatched builds keep working, and while the
interface has no users that is machinery bought at the price of clutter in every struct. Whatever
policy replaces this at 1.0.0 can be designed then, against a real interface rather than a guess.

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
