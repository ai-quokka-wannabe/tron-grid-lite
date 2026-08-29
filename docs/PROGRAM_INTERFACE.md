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
| User     | The human at the window, who watches and never acts                       |
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

**A Program is named, never pathed.** The Grid takes an identifier and resolves it against a
directory it already trusts, applying the platform's own decoration — `<name>.dll` on Windows,
`lib<name>.so` on Linux.

**One identifier resolves to exactly one filename, and a Program's build has to produce that name.**
The Grid does not try a second spelling, because two candidate spellings would mean two possible
binaries for one name, and which of them wins is not a question worth having an answer to in the code
deciding what foreign library this process is about to run. Toolchains disagree here: MinGW prefixes
shared libraries with `lib` on Windows as well as on Linux, so a Program built with it lands as
`libquokka.dll` where the Grid wants `quokka.dll`. Set the target's prefix to empty there. The
refusal says so when it sees the other spelling beside the one it wanted, rather than leaving four
characters to be spotted.

The identifier alphabet is ASCII letters, digits, underscore and hyphen, beginning with a letter or
digit, and no longer than 64 characters. That is deliberately narrower than what a filesystem would
accept, and the narrowness is the mechanism: an alphabet with no dot, no separator and no colon in it
cannot express `..`, cannot express `/usr/lib/libc`, and cannot express `C:\windows\system32\...`, so
there is no traversal to detect and no check anyone has to keep being right about. Rejecting the dot
is what makes `..` unrepresentable rather than merely caught.

This matters because of where the identifier comes from. A roster read from a configuration file is a
file a downloaded creature pack can write, so it is genuinely untrusted input in a way `argv` from
the person who launched the process is not. Removing the capability rather than guarding it is the
same answer that worked for `tools/record_flyby.py`, where `--preset` and `--config` name choices
from constant tuples so that no path arrives from outside at all.

The trusted directory is `programs/` **beside the executable**, never beside the working directory.
That distinction is the whole of the confinement rather than a detail of it: a directory that
followed the working directory would let whoever chose where the Grid was launched from choose which
binary a roster entry named, which moves the question rather than answering it.

The directory holds as many Programs as a User cares to install, and the identifier selects one.
`--list-programs` reports what is in it and which of them the Grid would accept, by loading each
rather than by reading its name — a Program built against an older ABI is a stale file that looks
exactly like a current one, and only loading it tells the two apart. Nothing there is hidden: a
library that could never be accepted is listed with the reason rather than omitted, because a Program
silently absent from a listing is the one somebody spends longest looking for.

`--list-programs` performs every check below and then unloads, so a Program that will not load can
be diagnosed on its own rather than three seconds into a run; it stops short of `library_init`,
because that call carries the tick length and a check that invented one would hand a Program a
number the run would later contradict. `--program <name>` performs the same checks and then goes
on: it loads the Program for real and hosts its creature in Master Control's world.

The Grid loads the resolved file with `LoadLibraryEx` (Windows; the library's own directory is
on the search path, so a Program may deploy the runtime it needs beside itself in `programs/` -
rc-worm's panel deploys its Qt there - and the PATH is not) or `dlopen` (Linux; `$ORIGIN` in the
library's RPATH does the same) and resolves
exactly one exported symbol:

```c
/*! The single entry point every Program library must export with C linkage. */
const TglProgramVTable* tglGetProgramVTable(uint32_t requested_abi_version);
```

The Grid passes the ABI version it was built against. If the Program cannot satisfy that version it
returns `NULL`, the Grid logs the mismatch and refuses to run that Program. No negotiation, no
shims — a Program either speaks the current ABI or it does not load.

Everything else the Program needs is reached through the returned vtable, so a Program exports one
symbol and the Grid does exactly one symbol lookup.

Two obligations the returned pointer carries, both of which this document owes and the borrow rule
below does not cover:

- **The vtable has static storage duration and must remain valid until `library_shutdown` returns.**
  Every other pointer in this interface is borrowed for the call only; this one is not, and a
  Program returning the address of a temporary is conforming by the text of § Rules and dangling in
  fact for the whole run.
- **All five members must be non-NULL.** The Grid checks each one at load, refuses the library and
  names the missing member. That is the highest defensive value per line available anywhere in this
  boundary, for the plain reason that § Writing a Program instructs authors to hand-write the
  vtable, and a partially transcribed struct is exactly what hand-writing produces.

### Lifecycle

```text
1. The host starts, reads its creature roster, resolves Program library paths, dials Master
   Control.
2. dlopen / LoadLibrary the Program library.
3. tglGetProgramVTable(TGL_ABI_VERSION) -> vtable, or NULL -> abort this Program.
4. vtable->library_init(&library_info)               once per loaded library
5. For each creature body assigned to this library:
       vtable->program_rez(&creature_desc, &render_model)   -> opaque Program handle,
                                                               and the body's own shape back,
                                                               sent to Master Control as REZ
6. Per tick:
       a. Master Control steps every body once, and tells the host the tick whole: the rows,
          and the owner's letter of what each body felt.
       b. The host renders each creature's eyes into their own tiny render targets.
       c. The host gathers each creature's ears against the same hierarchy.
       d. The host reads the eye targets back and fills TglSenses, with the body senses.
       e. For each live creature:
              vtable->program_tick(handle, &senses, &actions)
       f. The host sends every TglActions up the wire as the intent for the tick the world
          will step next; Master Control clamps it - the validator is the only path in.
7. On creature death or Grid shutdown:
       vtable->program_derez(handle)
8. vtable->library_shutdown()                        once per loaded library
9. dlclose / FreeLibrary.
```

Steps 4 and 8 are called exactly once per library, regardless of how many creatures that library
drives. Steps 5 and 7 pair up strictly: every handle returned by `program_rez` receives exactly one
`program_derez`. A `program_rez` that returned `NULL` produced no handle and therefore never
receives a `program_derez` — the Grid skips that body and there is nothing to release.

**Only step 6e is inside the per-creature loop, and the two steps that sit outside it carry the
whole of the ordering argument** - which is now Master Control's to keep. Physics advances once
for every body, because advancing it per creature cannot express two creatures touching each
other at all, and it makes roster iteration order physically observable in the world. Clamping
and staging happen after every Program has been called, for the mirror reason: applying inside
the loop would let the first creature's action move the world before the last creature's call,
which makes "an action takes effect on the next tick" true for one creature and false for the
rest.

---

## The C ABI Boundary

### Rules

- **Plain C only, and the standard is C17.** ISO/IEC 9899:2018, also written C18 — chosen because
  it is the C standard C++20 itself names as its library baseline, so the Grid's side and a C
  Program's side quote one era of the language rather than two. Anything older meets the header's
  own `#error` rather than a fallback: a pre-C11 toolchain cannot check the layout assertions
  readably, and an ABI header that cannot check its own layout is a memory corruption with
  documentation. No C++ types, no exceptions, no RTTI, no templates, no `std::` anything crosses
  the boundary. A Program may be written in C++, Rust, Zig or anything else that can emit
  C-linkage exports, but the ABI itself is C.
- **No exceptions across the boundary.** A Program written in C++ must catch everything at its own
  export boundary. An exception unwinding into the Grid is undefined behaviour and will very likely
  terminate the process. [STYLE.md](../STYLE.md) § Error Handling states the same rule from the
  Grid's side and asks this header to enforce it rather than assert it: `noexcept` on a vtable
  member is part of the pointer's *type* from C++17 onward, so a C++ Program that omits it fails to
  compile instead of being asked politely. Spelling that in a header which must also compile as plain C
  costs one macro expanding to nothing on the C side — the only compiler-conditional construct the
  header would contain, and the only available mechanism that makes the rule unrepresentable to
  break.
- **No memory ownership transfer.** Every pointer in a struct passed to a Program is owned by the
  Grid and is valid only for the duration of that call. The Program must copy anything it wishes to
  keep. Symmetrically, the Grid never frees anything a Program allocated.
- **One version number, checked once, and it moves whenever the header does.**
  `TGL_ABI_VERSION` catches a stale library; it does not express compatibility. A constant
  that never moves catches nothing, because the stale library carries the identical constant — see
  [Versioning](#versioning), which holds the mechanism that keeps the number honest. There are no
  per-struct size fields and no compatibility shims.
- **Fixed-width types only.** `uint32_t`, `int32_t`, `float`, `uint64_t`. No `int`, no `long`, no
  `size_t`, no `bool`, no enums with unspecified underlying type, no bitfields.
- **Standard layout, and no padding anywhere.** Every struct here is a plain C standard-layout
  type of fixed-width members, so MSVC, GCC and Clang lay it out identically on both supported
  platforms — and every struct asserts that its members account for all of its bytes. Where
  alignment would otherwise insert a hole, a named and documented padding member takes the slot,
  because a struct with indeterminate bytes is neither hashable nor recordable, and this
  repository hashes things.

### Vtable

`TglProgramVTable` is declared in
[the header](../libs/program-abi/include/tgl/tgl_program_abi.h), like everything else in the
interface. This document deliberately does not restate it — the listing this section used to
carry proved the rule by drifting three ways at once: it still named the version constant
`TGL_ABI_VERSION` pinned at 1, showed a vtable without the `struct_size` and
`abi_version` header members, and spelt `program_rez` without the model it now carries back.

The whole interface is one exported symbol, a two-member vtable header — `struct_size`, the one
piece of compatibility machinery in the file, and `abi_version` — and five `noexcept` function
pointers falling into two scopes: `library_init` and `library_shutdown` bracket the loaded library, while `program_rez`,
`program_tick` and `program_derez` bracket the life of one Program on one body. That split is what
the rest of this document rests on, so it is written into the struct rather than left implied.

The two library-scope members keep plain names on purpose. They are `LoadLibrary`/`dlopen` and
`FreeLibrary`/`dlclose`: facts about Windows and Linux rather than events on the Grid. Tron words
name events on the Grid; plain words name events in the operating system.

A change to any name, type or member in this vtable is a change an already-built Program could
notice, so it moves `TGL_ABI_VERSION`. See [Versioning](#versioning).

### Library and creature descriptors

**The layouts are not written down here.** They live in
[`libs/program-abi/include/tgl/tgl_program_abi.h`](../libs/program-abi/include/tgl/tgl_program_abi.h),
which is the file both the Grid and every Program compile, and it is the only place any member of the
interface is spelled out. A struct copied into prose is a second copy of a fact with nothing holding
the two together — the shape of defect this repository loses most of its time to — and the copy is
the one people read. What belongs here is the reasoning, which the header states only in summary.

`TglLibraryInfo` carries what a Program library may want before any creature exists;
`TglCreatureDesc` describes one body, and `TglEyeDesc` and `TglEarDesc` describe its sensors. All of
them are handed over once, at rez, and never again.

**The Grid ticks at 32 Hz**, so `dt_seconds` is 0.03125 exactly. The rate was chosen for that
word rather than for feel: 0.03125 is representable in binary32 and so is a four-substep
0.0078125, which makes `tick * dt` exact for a hundred and forty-five hours. A recording's
timestamps are therefore the same numbers coming out as going in, where 25 Hz (0.04) or 50 Hz
(0.02) would accumulate an error somebody eventually has to explain in a replay.

**`dt_seconds` is a constant the Grid supplies, and the Grid does not measure it.** There is no
actual value distinct from the nominal one, and the wording is deliberately not "nominal, with the
actual value repeated each tick". A Grid that timed a tick with a `steady_clock` and handed the
result to every Program would be the single largest replay hazard in the project, and it would
arrive dressed as a convenience.

**It is one number spelled one way, and that is a decision rather than an oversight.** There is no
`tick_rate_hz` beside it, tempting as the convenience is: an integer hertz cannot represent every
reciprocal `dt`, so the two spellings would disagree in the last bits and then drift. The number
still spans a C ABI — `TglLibraryInfo::nominal_dt_seconds`, `TglSenses::dt_seconds`, and the
Grid-side constant the physics step integrates against — and those are held together by a
`static_assert` against the header's literal, because a comment saying "must equal" is not a
mechanism.

Note the direction of control: the **Grid** decides how many eyes a body has, how many samples each
one takes, in which directions and in how many channels, and hands the lot to the Program. A Program
does not request an eye. Sensor geometry is a property of the creature's body, not of its cognition,
and bodies are the Grid's business.

**No sensor preset in [PERCEPTION.md](PERCEPTION.md) is a rectangle, which is why an eye is a sample
list and there is no raster layout at all.** `elegans` is two body-referenced samples, the three
insect presets are direction lists by construction, the rodent field is a uniform buffer wrapped
around two thirds of a sphere rather than a frustum, and the macropod is a streak with an embedded
area centralis plus a coarser periphery — a graded specification that document calls more honest than
one number. A rectangle plus two field-of-view angles cannot express any of them.

A raster is the degenerate sample list whose directions happen to form a grid, so nothing is lost by
leaving it out, and something is gained: one code path, and no layout enumeration for a Program to
branch on.

It is the wrong shape for a foveated eye specifically, and not merely a coarse one. A perspective
raster spends a constant *tangent* step per column, so the solid angle one sample covers shrinks
towards the edges: the periphery is sampled finest and the centre coarsest, which is the exact
inverse of every foveated eye in that document. A sample list has no such gradient to fight, which
is why rule 4 there prescribes one.

---

## Senses

A creature senses through zero or more small render targets, one per eye, each rendered by the same
ray-tracing compute path that renders the User's window, from that eye's own position. Sizes,
channel counts and layouts vary by preset, and some eyes are sample-direction lists rather than
rasters. They are deliberately tiny — a few samples to a few tens of thousands — because animal
eyes are also modest in the terms that matter here, and because a creature must be cheap enough to
simulate in numbers. See `docs/PERCEPTION.md` for the reasoning behind the sizes and the sensor
model.

`TglSenses` and the two view structs are declared in
[the header](../libs/program-abi/include/tgl/tgl_program_abi.h), for the same reason the descriptors
are: one copy, compiled by both sides. What follows is why each modality is shaped as it is.

**Thermoreception is a spherical integral, and the arithmetic has to be faced rather than rounded
away.** One ray is one direction, and choosing that direction at random is a one-sample Monte Carlo
estimate — precisely what Determinism and Replay forbids, because the answer would then change with
the draw. The direction set is therefore fixed, drawn from the same spherical Fibonacci set the
acoustic gather uses, and how many directions a body can afford is a property of that body rather
than a constant in the ABI. A fixed set carries the stationary structured bias ACOUSTICS.md records
against its own fan: it misses a small warm thing the same way on every tick, and that error does not
average out over time.

**Touch is a list of contacts, not a summed vector**, because summing destroys the one thing touch is
for. A body lying along the floor contacts it in many places at once, and the sum of those is a
number that says "downwards" and nothing at all about lying down. Each contact carries where it
happened in the body frame and the impulse delivered there, so direction and strength arrive
together; the physics step computes both anyway, which makes the richer answer the cheaper one.
Since ABI v5 each contact also names its face - the world normal, how deep the body stood past it
before being stood back, and the slip, the body's velocity along the face - so a foot planted, a
landing, a flank dragged along a wall and a brush past another creature all read differently; the
slip is the sense of scraping, and the world sounds it as a scratch.

That is also the only sense here that reports a point **on** the creature rather than a direction
away from it, and it is how a Program may come to know the extent of its own body — by bumping into
the world, which is how an animal learns it too.

**The joints report themselves, and the report is the encoder's, not the command.** Since ABI v9
`joint_angles` carries the angle every servo actually holds, in the sign `joint_targets` asks in:
what the joint did rather than what it was asked. The two disagree whenever a servo is still
swinging to its target, stalls short of it past its torque, or is bent by the world - a wall, a
neighbour, the floor's grip on a runner - and that disagreement is the only readback a gait has,
the same way `body_forward_speed` disagrees with `desired_forward_speed`. The world that holds
the servos reports it, in the same letter as the force and the contacts; the Grid copies and
derives nothing, so that a servo's load, which no pose can yield, joins it on the same channel -
and since ABI v10 it does: `joint_torques` is the torque each servo holds its angle with at the
tick's end, signed, at most the declared maximum and exactly that when the servo stalls. A
motor's current sense, a tendon's organ. Stall is the Program's to notice by comparing the two
numbers it already has; no flag says it for it, because none is needed and a flag would be a
sense the body does not have.

### The senses at a glance

| Sense | Field | What it physically is | Status |
|-------|-------|----------------------|--------|
| Vision | `eyes` | Rays traced from each eye through the shared BVH | Phase 6 |
| Proprioception | `body_forward_speed`, `body_vertical_speed`, `body_turn_rate`, `joint_angles`, `joint_torques` | The body's own actuator state; since ABI v9 every servo's own reading, since v10 its load | Phase 6 |
| Vestibular | `specific_force`, `angular_velocity` | Derivatives the motion integrator already has | Phase 6 |
| Touch | `contacts`, `contact_count` | Where the body was struck this tick, and how hard | Phase 6 |
| Thermoreception | `irradiance` | A fixed quadrature over the whole sphere, undirected | Phase 6 |
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
- Nothing here is a valuation. The Grid reports where the body was struck and with what impulse;
  whether that amounts to pain is the Program's business, and pain is cognition wearing a sensory
  costume.
- **A Program is never handed a diagram of its own body** — no segment lengths, no rest pose, no
  kinematic tree, and no joint angles. A chain's trailing segments are placed by the world and are
  not reported back as a sense: a chain feels, sees and hears with its head, and what its tail is
  doing it may infer from how it moved. Nowhere in an animal is there such a model either. What a
  creature gets is where it was touched, how it is moving and what it can sense, and the shape of
  itself is something it may learn rather than something it is told. Joint angle and joint rate
  arrive as senses with a solver that bends a joint, not before.
- Every field is populated. A modality that does not exist yet has no field waiting for it — the
  struct describes what the Grid can currently sense, and gains members when that changes.
- **An ear delivers an impulse response, not a sound.** It says how much energy arrived in each
  frequency band at each delay. Working out that two arrivals four milliseconds apart mean a surface
  seventy centimetres away is the Program's job, exactly as it is the Program's job to decide that a
  bright cyan band is a wall.
- **One tick is a discontinuous function of state, and deliberately so.** Contacts are unilateral:
  the active constraint set changes between substeps, so an arbitrarily small change in a body's
  position can change which surfaces it is touching and therefore what it senses. This is the world
  being honest — a foot is either on the ground or it is not — but it means anything on the Program
  side that differentiates through the simulator sees cliffs rather than slopes. Better learned here
  than rediscovered in another repository.

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

`TglEarDesc` and `TglEarView` are declared in
[the header](../libs/program-abi/include/tgl/tgl_program_abi.h), like everything else in the
interface. The decisions behind them:

**An ear has a position and no axis.** The gather casts a full spherical direction set from a point,
every surface is a perfect acoustic mirror, and there is no directivity term anywhere in the acoustic
model — so an ear facing backwards hears exactly what one facing forwards hears. A `direction` member
would be a field the Grid reads and nothing acts on, in a document whose own rule is that every field
is populated. It arrives when directivity does, and the version bumps then.

**Band edges come from the body's audiogram, not from room-acoustics convention**, and the
per-band atmospheric absorption is authored beside them because both follow from the same audiogram:
a creature that hears in a different part of the spectrum needs different numbers in both. There is
deliberately no function on the Grid turning a frequency into an absorption — the air is fixed at
20 °C and 70 % relative humidity, so these are constants resolved into this ear's bands.

**The response's span is a hard edge rather than a soft one.** An arrival later than
`bin_count * bin_seconds` is dropped rather than folded into the last bin, so the last bin is an
ordinary bin and never a catch-all. The span is sized against the Grid's own acoustic horizon rather
than against a listener's patience: `Acoustics::RANGE_METRES` of total path is 58 ms at 343 m/s,
inside the 64 ms that `Acoustics::BIN_COUNT` bins of `Acoustics::BIN_SECONDS` cover, so nothing the
gather can produce falls off the end. An ear asking for a shorter span is asking to be deaf to its
own late arrivals.

**Energy is band-major**, element `[(band * bin_count) + bin]`, because that is what a listener
walks: finding arrival times within a band means stepping through bins, and this layout makes that
one contiguous run per band where the transpose would make every step a stride.

**The unit is defined once, in the header**, at `TglEarView::energy`: the primary neon tube, whose
authored strength is 1.0 by definition, the Grid's only reference level, shared with
`TglActions::vocalisation_strength`. This document deliberately does not restate the definition —
a unit stated in two places is a unit that can disagree with itself — and the acoustics it follows
from, including why a bin cannot be relative to "the emitting source", is derived in
[ACOUSTICS.md](ACOUSTICS.md) where the model is. A bin no sound has reached reads zero, which is
the physical answer rather than a sentinel.

An `ear_count` of zero is a legitimate body, and is the correct specification for all three insect
presets: an ear costs a gather, and a body with nothing worth hearing should not pay for one. The
ear members sit under their own `/* -- Hearing -- */` banner in `TglSenses` rather than beside the
eyes, because the modality grouping is what a reader transcribing the struct navigates by.

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

**And the delivery is built.** A call staged this tick sounds on the next, like every other action,
and that tick's ear views carry its whole response on top of the hum: every ear on the roster
receives it — the caller's own first and loudest, which is the only readback the voice has or
needs — with the direct arrival graded by an occlusion probe rather than cut by a single ray (a
thin post dims a call; it does not silence it; and the grading is occlusion sampling, never
diffraction), and one validated first-order image per terrace level and per outward box face, each
constructed by mirror arithmetic and then confirmed with rays through the same hierarchy every
other sense reads. The mirrors include the floor's own risers: every terrace wall on the Grid is
genuinely vertical and stands in the enumeration, so echolocation is **monostatic as well as
bistatic** — a caller hears its own ping come straight back off the step in front of it, ranges it
by the delay, and hears every other creature's calls and echoes besides.
[ACOUSTICS.md](ACOUSTICS.md) § The Terraced Floor carries the wall geometry and its measured
numbers.

**And since ABI v6 an ear also reports its discrete arrivals.** Beside the histogram, each
`TglEarView` carries up to `TGL_EAR_ARRIVALS_MAX` records of `TglArrival` — one per call arrival
that tick, direct paths and validated images alike, the loudest kept when the ear is full: the
exact onset in seconds (the path over the speed of sound, with the sub-millisecond structure the
bins destroy, so two ears a few centimetres apart time one caller tens of microseconds apart and
the difference is its bearing), the radial velocity along that path (positive receding, negative
approaching; Doppler as a number, because at creature speeds the shift is under one per cent of
pitch), and the energy the arrival deposited per band. The hum records none — a sourceless bed
has no onset and no bearing. `arrivals` is NULL when `arrival_count` is 0; both are borrowed for
the tick like everything else here. [ACOUSTICS.md](ACOUSTICS.md) § What a Creature Ear Needs
carries the physics.

**Scratches reach the ear too — as energy, never as an arrival.** Every slide the world sounds
— a body's feet, a scrape along a riser, and since the chain every dragged segment of a worm,
the listener's own body included — is delivered onto the histogram exactly as a call is, from
where it happened, through the scraping body's own hull. But a scratch is the one sustained
source on the Grid (the section above), noisy and modulated by the gait, with no onset to time:
it deposits energy in the bins its distance dictates and contributes no `TglArrival`. So a worm
hears its own spikes drag along the floor, and hears another worm's, and can tell that it does
and roughly how loudly — and cannot range either, which is the honest physics, not a gap. A
Program that wants to know where its own body is dragging has its contacts for its head and
its ears for the rest, which is proprioception by sound and exactly the kind of thing a mind
is for.

One scope caution, because it is easy to violate by accident. The ABI delivers **energy per band per
time bin per ear, plus the arrivals' onsets, radial velocities and energies, and stops.** Anything that names a source, separates streams, reports "a wall is
three metres to your left", or performs auditory scene analysis of any kind belongs in a Program
repository. The Grid makes localisation *possible* by delivering two ears with their own signals; it
does not perform it.

### Chemoreception: deliberately absent, and worth revisiting

Smell has no place on the Grid as it stands because there is no chemistry in it — only geometry,
light and sound. Adding it would mean a diffusing scalar field, the first subsystem the BVH
cannot help with at all.

It is recorded here rather than dismissed because there is a real argument on the other side. The
simplest sensor preset is named after an animal that navigates chiefly by chemotaxis, not by light,
so the simplest possible creature is arguably specified with the wrong sense — and gradient
following would be a far gentler first problem for a Program than interpreting an image.

---

## Actions

An action is physical intent, expressed in the body frame, and nothing more.

`TglActions` is declared in
[the header](../libs/program-abi/include/tgl/tgl_program_abi.h), like everything else in the
interface: a desired forward speed, a desired turn rate, and the loudness of one call. This
document deliberately does not restate the struct — the header is the specification, and the one
listing this section used to carry proved the rule by drifting: it still showed the vertical
actuator after the header had retired it. The decisions behind the fields:

- **The servos, and which actuators a body has (ABI 8, Etape 8 movement 3).** `TglActions`
  carries `joint_targets[7]`: the angle each servo is asked to hold this tick, joint k between
  segments k and k + 1, positive bending the chain to the creature's left. `TglCreatureDesc`
  carries `max_joint_angle` and `max_joint_torque`, the Grid's limit for the class. *Which*
  actuators a body has follows from the body it brings: a chain (segment_count above one in
  its model) is a row of servos at its pivots and has no velocity actuator - its
  `desired_forward_speed` and `desired_turn_rate` are read by nothing - while a body of one
  segment has the velocity actuators and no servos. The descriptor precedes the model, so it
  states the Grid's limit for each class; once the model is validated the Grid tells the
  world which class this body has, with a bound of zero for the other. The gait - which joint
  bends when - is the Program's own; the Grid carries it to the world as it is, the world
  clamps each target to the swing and holds it with no more than the torque, and how far the
  body gets is the floor's answer, reported by the senses. rc-worm's `Gait` is the first: a
  travelling wave of joint angles, the panel's forward and turn not a speed and a heading but
  how hard the wave runs and which way the body bends.

**There is deliberately no vertical intent.** Height is physics' business — gravity, the floor and
whatever the body ran off — and on a Grid with no water and nothing climbable a vertical actuator
clamped to zero for every plausible body was a field the Grid read and nothing could act on.
`TglSenses::body_vertical_speed` still reports what gravity is doing.

**A call is a call, not a channel.** The Grid emits it as a single burst from the creature's own
position and it is over. There is no field for its duration, spectrum or waveform, because there
is no waveform anywhere on the Grid — what the voice sounds like is a fact about the body,
authored on the Grid's side as the ears are, and the action says only whether it was used and how
hard. Setting it every tick produces a train of calls, which is what an echolocating animal does;
it does not produce a continuous tone, and nothing on the Grid can.

The Grid **clamps** every field to the body's physical limits and then feeds the result to the
physics step as intent, not as a teleport. A Program asking for one hundred metres per second gets
whatever its body can actually manage. Non-finite values (`NaN`, infinities) are treated as zero.

The limits themselves are a property of the body, fixed when the Program is rezzed, and they are not
negotiable by anything a Program returns. This document promises clamping in every action field and
once more in the lifecycle; the numbers it clamps against are the body's, and until a body exists
they are the emptiest promise in the interface.

**The acceptance test for the whole physics subsystem is written into the senses rather than into a
benchmark: `body_forward_speed` must disagree with `desired_forward_speed` whenever the body is
pushed rather than driven.** If a creature dragged against a pillar reads its own command back, both
proprioceptive channels are carrying zero bits of information, and no amount of stacking or
throughput proves otherwise. Everything else about the physics — how many contacts, how many
substeps, how fast — is negotiable; this is not.

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

## The Body's Own Shape

`program_rez` hands the Grid something back: alongside the returned handle, a Program may fill the
`TglRenderModel` the Grid passes in — vertices, triangles and materials, in the Grid's own
continuous material model — and that shape becomes the creature's body in the world. The struct and
its validation contract are declared in
[the header](../libs/program-abi/include/tgl/tgl_program_abi.h); what belongs here is the
reasoning.

**This is the one thing in the interface a Program authors rather than receives, and the inversion
is deliberate.** The Grid decides what a body can do and sense — bounds, ears, eyes — because
capability is physics and physics is the Grid's. What a body looks like lives with the Program,
because a creature's shape belongs in the creature's own repository, and a Grid that carried every
creature's mesh would couple its releases to every body ever modelled. A Program rezzes into the
Grid with its own appearance, which is the oldest sentence in the fiction this project is named
for.

**The Grid keeps what it must.** The material model is the Grid's — a body is built of the same
mirror, glow and glass every surface of the Grid is, and nothing in the model can express a
texture because nothing on the Grid can. The frame is the body frame, in metres, one rigid piece
per segment. And the Grid validates the whole model before accepting any of it: an index out of
range, a value that is not a number, a triangle with no area, a material Snell's law cannot bend,
or a chain that lies about itself refuses the rez outright — accepted entire or refused entire,
because a silent repair would ship a body its author never saw, and a poisoned hierarchy fails
somewhere else entirely on behalf of every creature at once.

**A body may be a chain (ABI v7, the owner's ruling of 2026-08-26).** The model declares
`segment_count` — the head counted, one to `TGL_SEGMENTS_MAX` — and `segment_spacing`, the
metres between consecutive segments' origins along the head's path; every segment wears this one
mesh. The head is the rigid body the world's physics steps. Every trailing segment is *kinematic
trail*: Master Control places it one spacing further back along the path the head actually
walked, so the chain bends where the head turned and undulates as the User weaves — nothing is
articulated, nothing is solved, and TOPOLOGY.md's deferred rigid-body solver stays deferred. The
joint between two segments is the model's own business: a stub on each of the two spikes that
meet, authored into the mesh, so the wire carries poses and nothing else. A single body declares
one segment and no spacing; a chain declares a positive one; anything else refuses the rez.

**Offering no model is a legitimate body, and it is today's default.** A zeroed model is a creature
with no visible shape, exactly what every body has been until now.

**"A Program is never handed a diagram of its own body" survives unchanged.** The Grid still tells
a Program nothing about its shape, and the senses still report only what the body feels and sees.
A Program that authors its model knows only what it chose to write — the same epistemic position a
genome is in — and what its body actually does in the world it still learns the way an animal
does, by bumping into things.

**And the staging is built.** An accepted model becomes a hierarchy of its own at rez — built once,
because a rigid body's hierarchy never needs rebuilding — and stands in the world as one instance
per segment, consecutive, all sharing that hierarchy, each transform following its segment's pose
each tick. Other creatures see it, their calls shadow behind it, and the hum bends around it. A
creature's own senses see *through* its own hull — the whole chain of it, head and trail —
deliberately: its ears sit on its body and its voice leaves from inside it, so a hull that blocked
its own sensors would deafen and gag the body it belongs to, and a tail that blocked them would
deafen it the moment it turned. What is lost with that first cut is the creature's
own head shadow, which needs sensors modelled proud of the hull — the body author's business, not
the Grid's.

---

## Threading and Ownership

- `program_tick` is called **serially, in roster order, on one thread whose identity is fixed for
  the whole run** — the header's own words, repeated here only to attach the reasoning. Serial
  because replay is claimed and bodies push each other, so no result may depend on who finished
  first; a fixed thread so that a Program caching thread-local state, or hosting a runtime that
  cares which thread calls in, has something solid to stand on.
- **A parallel tick is a widening this version does not license.** The design would survive one —
  each call writes its own `TglActions` slot, the Grid applies them in fixed roster order after
  every call has returned, so completion order can never matter — but a Program built against this
  version is entitled to the serial promise, and widening it is exactly the change
  `TGL_ABI_VERSION` names as a bump: a threading promise widened or narrowed.
- `library_init`, `program_rez`, `program_derez` and `library_shutdown` are called from a single
  thread with no other Program call in flight.
- `program_tick` **must not block.** The reason is not a tick budget — there is no budget and
  nothing here runs on a clock. It is that a blocking Program stalls the whole Grid permanently, and
  the Grid cannot kill the thread without corrupting the process. A merely *slow* Program is a
  non-problem: `dt_seconds` is supplied, nothing is dropped and no quality degrades. A Program that
  needs long-running work must do it on its own thread and let `program_tick` read the most recent
  result.
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

### What the Grid will not do about a misbehaving Program

The budget goes to **attribution — naming the culprit — and never to remedy.** Four things follow,
and the last is the one every watchdog discussion drifts toward:

- **Every `program_tick` may be timed and outliers logged, but the measurement must never be acted
  upon.** The comment saying so belongs in the code, because that is the exact line a future change
  will be tempted to cross.
- **No timeout-substituted actions.** Feeding zeros, reusing last tick's actions, or skipping a slow
  creature all make the simulation a function of the OS scheduler, unreproducibly. This is the one
  design that must never be adopted.
- **No watchdog may cancel or detach a hung tick.** `TerminateThread` leaves heap and
  critical-section locks permanently held, and a detached tick still holds `TglSenses` pointers this
  document declares valid only for the call, into buffers the Grid is about to reuse. Detect and
  log; never remedy.
- **`catch (...)` around a vtable call sees no access violation and no divide by zero** under the
  compiler settings this project builds with, yet is easily believed to. The structured-exception
  version that *does* see them yields a corrupted process which keeps simulating, producing
  plausible wrong numbers instead of a crash — worse than the crash.

What is worth doing instead is cheap. [STYLE.md](../STYLE.md) § Error Handling already forbids an
exception crossing this boundary in either direction, and marking the host-side call sites
`noexcept` is the enforcement: it catches nothing, but it turns an unwind arriving from a plugin
from undefined behaviour into a terminate at a known instruction. Beside it, write a breadcrumb —
library path, `creature_id`, tick — before each call, paired with a terminate handler that logs it.
That is what turns "the Grid crashed" into a named culprit.

Program authors should also be warned away from `DllMain` and non-trivial static initialisers: work
there runs under the loader lock, and a deadlock there cannot be timed out at all.

### Hot reload

Not supported. Restart the Grid to load a different Program build.

---

## Determinism and Replay

The interface is designed so that a run can be reproduced:

- Each creature receives a `random_seed` when its Program is rezzed rather than seeding itself from
  the clock.
- The Grid's ray tracing is Whitted-style and deterministic: three surface kinds (mirror, emissive
  and simple glass), no Monte Carlo sampling, no temporal accumulation, no denoiser. The same
  camera pose on the same Grid yields bit-identical pixels — **on the same device**. See below.
- `dt_seconds` is supplied explicitly rather than measured by the Program.

Consequently, recording every `TglSenses` a Program saw and replaying the sequence into the same
Program build must reproduce the same `TglActions` sequence. A Program that fails this is
non-deterministic internally, which is allowed but should be a deliberate choice.

### What is actually guaranteed, and to whom

**The guarantee is replayability, not pixels.** A run is reproducible from `(seed, initial state,
input log)` on one build and one device. Bit-identical pixels are a *consequence* of that, per
device and per build, and their value is as a refactor oracle rather than as anything a creature
benefits from.

That ordering matters because the two get conflated, and the conflation makes an easy question look
like a dilemma. Three properties travel under one word here, and only the first is promised:

1. **Reproducibility** — the same inputs and the same seed produce the same outputs. This is what is
   claimed.
2. **Absence of stochastic content** — nothing in the world is random. This is strictly stronger and
   strictly more expensive, and it is what the code has. It is a fact about the code rather than a
   vow binding subsystems nobody has written; the honest wording is that there is no *unseeded,
   unrecorded or globally-stateful* randomness anywhere.
3. **Cross-device bit-identity** — refused below, and correctly, because the expensive half of
   determinism buys nothing here.

The price of determinism lives almost entirely in that third tier, which is why the published horror
stories do not price this project's decision: they are stories about replacing every float with
fixed point, or forking an engine onto soft floats. Single-device, same-binary determinism is nearly
free, and here it was not even bought — the aesthetic caused it. Smooth surfaces mean no Monte
Carlo, no Monte Carlo means no denoiser, no denoiser means determinism. **This project is not
maintaining determinism; it is noticing it.** The corollary is that unbanning randomness would
unlock no rendering quality either: soft shadows and glossy reflections are forbidden by the
aesthetic, not by the vow.

The measurable runtime cost is three floating-point operations per acoustic ray, inside
`--benchmark`'s own spread — and cost-*negative*, because the arithmetic that pays it also removes
roughly 6e-4 radians of genuine float error.

### The boundary, written before the first line of physics

> The simulation is reproducible from `(seed, initial state, input log)` on one build and one
> device. The presentation is not, and **nothing in the simulation may read the presentation.**

**Inside:** physics integration, collision detection, contact resolution, constraint solving;
everything producing a `TglSenses` buffer, visual and acoustic alike; Grid generation, spawn state
and every stochastic quantity in the world; the tick itself.

**Outside:** the User's view, overlays, bloom, tone mapping, post-hoc high-quality re-renders,
profiling instrumentation.

Half of that line is already drawn — creature sensors are exempt from tone mapping — and the other
half needs three rules to stay drawn:

- **Tick count is driven by the simulation or by the replay log, never by a wall clock.** This is
  where a fixed-timestep accumulator's spiral-of-death clamp silently readmits nondeterminism,
  because dropping accumulated time makes the number of ticks executed depend on machine load.
  `dt_seconds` is supplied explicitly for exactly this reason. A fixed `dt` is also a physics
  correctness decision independent of determinism — integrator stability, spring constants,
  penetration slop and constraint stiffness are all tuned against a specific `dt` — so choosing it
  costs nothing this project wanted.
- **"Determinism forbids multithreading" is false.** It forbids *unstructured* multithreading:
  atomic accumulation into shared state, work stealing whose result depends on who won the race,
  thread-count-dependent partitioning, reductions applied in completion order. Parallelism is
  permitted where the merge is order-independent or the partition is fixed — which is exactly what
  the acoustic histogram's integer accumulation already is. A replay format must record the worker
  count, because deterministic multithreaded physics is not thread-count independent, and it must
  record the substep count for the same reason: contacts solved at zero compliance take their
  stiffness from the substep count, so a replay recorded at one count does not reproduce at another
  and must be refused rather than allowed to diverge.
- **If physics ever rides the compute path it ships as a flagged non-deterministic fast mode, with
  the host path retained as the reference implementation** for replay, regression and a
  `--verify-physics` check — the same host-against-device shape `--verify-acoustics` and
  `--verify-scene` already prove twice. Committing *unconditionally* to deterministic GPU physics
  would mean committing to host physics permanently: the integer-associativity escape hatch that
  rescued the acoustics has no analogue for a signed, wide-dynamic-range contact-impulse
  accumulator.

The cost of all this is a function of *when* it is decided, not of what is decided. Relaxing
determinism later is a flag; re-acquiring it after a solver exists is a rewrite.

One bound belongs beside the claim rather than left to be inferred: world replay is bit-identical
for the same **build**, not merely the same device. The hierarchy builder rests on `std::partition`
and `std::nth_element`, neither of which specifies a permutation, so triangle layout is a property
of the standard library implementation — identical from the same binary, not guaranteed between two.

### Determinism is worth nothing to a learning Program, and a great deal to whoever debugs the Grid

This is worth stating flatly because the opposite is widely assumed. A Program gains nothing from
bit-identical rendering: the reference digest is a SHA-256 over 8-bit images, so the guarantee is
quantised to 1/255 before anyone reads it, and gradient noise, weight initialisation and batch order
are not in the same universe as a sub-ulp radiance difference. Anyone selling determinism as a
training benefit is wrong.

What it does buy is measurement. In reinforcement learning the outcome signal is noisy enough that a
real bug and an unlucky seed produce the same-sized effect — ten identical runs of one published
algorithm, differing only by seed, split into two statistically distinguishable groups with
non-overlapping learning curves — which is precisely why whoever writes the Grid needs the Grid
removed from the suspect list. A reproducible environment is also what makes paired comparison
possible at all, and paired comparison is what buys statistical power in the few-run regime a solo
project necessarily occupies.

The reverse concern, that a deterministic environment invites a Program to memorise a trajectory
rather than perceive, is real and has a documented history — but nobody in the literature answered
it by making the engine irreproducible. They kept a bit-exact core and added a **seeded, declared,
parameterised** noise layer on top. That is the shape any variation here takes: randomise Grid
content — layout, materials, light placement, acoustic geometry, spawn states — never the renderer's
arithmetic. The honest framing is not "determinism versus randomisation" but **reproducible
randomisation versus unusable randomisation**, because a noise realisation that cannot be re-created
on demand forbids the best uses of randomisation and permits none of them exclusively.

### The shape any randomness here takes

Indexed, not streamed: a value is a pure function of `(seed, stream, index)` rather than the next
draw from a shared generator. That form does not care who calls it, in what order, or on which
thread, which makes it *more* reproducible than a global generator and parallel-safe for free. The
pattern is already here three times without being named — the acoustic direction set is a closed
form indexed by ray number, the lattice relief is an integer hash taking a seed, and each creature
receives its seed at rez rather than from the clock. Two rules come with it, and both are cheaper to
adopt than to retrofit: the seed goes into every replay header, and **any stochastic term reaching
the visual path breaks the reference digest unless the recording mode pins the seed.**

### Determinism is per device, and this is measured rather than assumed

**The pixel guarantee is bounded by the GPU, its driver, and the build.** Hold those fixed and the
same pose gives the same bits, every time — that part is real, and worth keeping. Change the GPU and
it does not.

**Cross-device bit-identity is not a goal, because it is not reachable.** IEEE-754 pins `+`, `−`,
`×`, `÷` and `sqrt` exactly; it pins neither whether a multiply-add is *contracted* into a single
fused instruction nor how `sin`, `cos`, `pow` and friends are implemented. Two vendors' shader
compilers make different choices there, both valid, and the difference is a fraction of an ulp per
operation. A ray tracer then multiplies that by every operation along a path. Forbidding contraction
and hand-rolling the transcendentals would narrow the gap and would cost real performance for a
property nothing here needs — which is a trade this repository does not make.

So the honest target is **small and measured, not zero**, and the measurement is below.

Measured on the reference machine, the same twelve frames at 640x360 rendered on an integrated AMD
Radeon and on a discrete GTX 1650 Ti:

| Difference per colour channel | Share of bytes |
|-------------------------------|---------------:|
| Identical | 83.6 % |
| 1–2 | 8.9 % |
| 3–8 | 3.9 % |
| 9–32 | 3.1 % |
| More than 32 | 0.5 % |

The largest single-channel difference was 224 of 255. The long tail is not noise in the shading: it
is a handful of rays at grazing angles striking a two-centimetre neon tube on one device and missing
it on the other, then a tone curve and a bloom chain amplifying the disagreement. The acoustic
gather behaves the same way and to the same degree — its agreement with the host reference is
0.000046 % on one device and 0.000069 % on the other, which are different numbers.

**What this does and does not break.** It does *not* break the replay conclusion above, and the
reason is worth being precise about: replaying recorded `TglSenses` feeds a Program the *pixels that
were recorded*, so no rendering happens and no device is involved. What it breaks is the different
and tempting idea of recording a *camera pose* and re-rendering it elsewhere to reconstruct what a
creature saw. That reconstruction is not the original, and on a different GPU it is visibly not the
original.

So: **record senses, never poses**, if a run must be reproducible on another machine.

One consequence is worth stating because it points the other way. A Program whose behaviour changes
when a pixel moves by two parts in 255 has learned the *graphics card*, not the Grid — and running
the same Program on both devices is a cheap way to find that out. Cross-device divergence is
therefore a robustness check that costs nothing to perform, and it sits squarely with
[PERCEPTION.md](PERCEPTION.md) rule 6: blur, aliasing and noise are the creature's problem to cope
with, not artefacts for the Grid to sand away.

### Three layers, and physics closes the loop between them

Most arguments about determinism are two people discussing different layers:

| Layer | Owned by | What is wanted |
|-------|----------|----------------|
| (a) the engine's physics and rendering | the Grid | reproducible |
| (b) the scenario and initial conditions | the Grid | randomised, but seeded |
| (c) the Program's own policy | the Program | recorded, or nothing else matters |

Bit-identical (a) is necessary for replay and worthless without (c). Engine determinism is therefore
**necessary but not sufficient**: the moment a Program loads a neural network with autotuned
kernels, the run diverges regardless of how perfect the Grid is.

**Physics closes the first feedback loop from the device back into the world, and that is a larger
change than it sounds.** Senses are rendered per device; actions derive from senses; actions move
bodies. Two machines running the identical binary therefore diverge into materially different
*worlds* within a few ticks, not merely different pictures — and the two reference GPUs already
disagree on about sixteen per cent of colour channels. The only cross-device-reproducible case is a
creature with zero eyes and zero ears, which the roster permits, and that is the outer boundary of
what could ever be promised. **Record senses, never poses** holds harder for a world than it does
for a picture.

---

## Versioning

**There is none, and that is deliberate until 0.1.0.**

`TGL_ABI_VERSION` is a build stamp rather than a compatibility statement. This interface
changes whenever it needs to — structs gain fields, functions change signature, semantics get
corrected — and none of that buys anyone backward compatibility, because nobody is owed it by a
project that has not reached its first release. Every Program that exists is built from this
repository, at whatever commit the Grid is built from. When the ABI changes, both sides rebuild and
the number moves with them. That is the whole story.

The constant is kept for the one job it can still do honestly: catching a **stale library**. A
`.dll` or `.so` left over from an older build, loaded against a Grid whose struct layouts have moved
underneath it, is memory corruption with no diagnostic. One integer compared at load time turns that
into a refusal and a log line.

**That job requires the number to move, which is the whole reason it moves.** A constant frozen at
`1u` cannot do it at all: the stale library was compiled against a header that also said `1u`, so it
answers the request, returns a vtable, passes the check, and delivers precisely the corruption the
paragraph above describes. A version that never changes is not a weak check but a check-shaped
absence of one, which is worse, because it reads as reassurance. So it bumps on **any** change a
built Program could notice: a struct that gains, loses or reorders a member, a signature, or a
semantic something could have relied on.

Discipline will not hold that, and this repository does not ask discipline to hold anything. The
mechanism is a CI step that hashes the header with the version line removed and fails when the hash
moves and the number does not — a duplicated fact with something holding the copies together, which
is the answer given every time one is found here.

- `tglGetProgramVTable` returns `NULL` unless the requested version equals the one the Program was
  built against. The Grid refuses to load that Program and says so. That single check is the entire
  mechanism.
- The Grid's own release version is unrelated and moves independently.

Real versioning arrives when there is something to version — a released Grid, and Programs written
by somebody who cannot simply rebuild them. Until then a number expressing *compatibility* would
record ceremony rather than compatibility, which is a different job from the stale-library stamp
above and must not be confused with it: one says "these two builds agree", the other says only
"these two builds are the same build".

There is deliberately nothing else — no per-struct size fields, no reserved members held back for
future growth, no negotiation. Those exist to let mismatched builds keep working, and while the
interface has no users that is machinery bought at the price of clutter in every struct. Whatever
policy replaces this at 1.0.0 can be designed then, against a real interface rather than a guess.

Practical advice while the interface is pre-1.0: rebuild your Program whenever the Grid is rebuilt.

---

## Writing a Program

1. Vendor [`libs/program-abi/include/tgl/tgl_program_abi.h`](../libs/program-abi/include/tgl/tgl_program_abi.h),
   or generate a binding from it. It is one self-contained C header depending on nothing but
   `<stdint.h>` and `<stddef.h>`.
2. Implement the five vtable functions and export `tglGetProgramVTable`.
3. Build a shared library named `<name>.dll` on Windows or `lib<name>.so` on Linux.
4. Drop it in `programs/` beside the Grid's executable and check it with `--program <name>`.

Nothing else is required, and nothing else is offered.

### Any language that compiles to a shared library

The interface is deliberately plain so that this is true rather than aspirational. Every member is a
float, a fixed-width integer or a pointer; there is not a union, a bitfield, an enum, a `bool`, a
native `int`, a `size_t` or a packing pragma anywhere in it. A Program exports **one** symbol, so a
binding has exactly one name and one signature to get right and everything else is a struct it fills
in.

| Language | Library | Exported symbol | Layout checked by |
|----------|---------|-----------------|-------------------|
| C, C++ | any shared-library target | `#define TGL_PROGRAM_IMPLEMENTATION` before including | the header's own assertions |
| Rust | `crate-type = ["cdylib"]` | `#[no_mangle] pub extern "C" fn` | `bindgen`, which emits layout tests |
| Zig | `addSharedLibrary` | `export fn` | `@cImport` of the header — the assertions come too |
| Go | `-buildmode=c-shared` | `//export tglGetProgramVTable` | the manifest |
| D, Nim, Odin, Ada, Fortran, Swift, Haskell, OCaml, … | whatever the toolchain calls a shared library | that language's C export | the manifest |

**The manifest is the point of that last column.** C and C++ get sixty-odd `static_assert`s that fail
the build the moment the header drifts; Zig inherits them through `@cImport`; Rust gets equivalent
tests out of `bindgen`. Every other binding declares the structs a second time with nothing holding
the copies together, and one wrong offset compiles perfectly and arrives as nonsense in a sensor
buffer. That is the worst shape a defect can take here — it looks like the creature is confused
rather than like the binding is broken.

So the layout is published as data in
[`libs/program-abi/abi_layout.txt`](../libs/program-abi/abi_layout.txt): every struct's size and
alignment, every member's offset and size, taken from the compiler rather than computed by hand. A
binding asserts against it in its own tests and gets back exactly what the assertions give C. A test
in this repository regenerates it and fails if it no longer describes the header, so it cannot go
stale silently.

```text
struct TglSenses 88 8
member TglSenses tick 0 8
member TglSenses eyes 8 8
...
```

### Four things a binding cannot see from outside

1. **The platform's ordinary C calling convention** — SysV AMD64 on Linux, Microsoft x64 on Windows.
   Whatever that platform's C compiler does by default; no `__stdcall`, no vectorcall.
2. **64-bit only.** Asserted in the header rather than assumed, and every offset in the manifest
   depends on it.
3. **Nothing may unwind across the boundary**, in either direction. C++ has `noexcept` on every
   function-pointer type, which makes it a compile error to get wrong. Rust arrives there without
   being asked, because an unwind out of an `extern "C"` function aborts rather than crossing.
   Everything else has to give its own answer.
4. **The filename decoration is the Grid's, not the toolchain's.** One identifier resolves to one
   filename. Rust's `cdylib` already produces exactly the right names on both platforms; MinGW
   prefixes `lib` on Windows and has to be told not to.

### Two more rules for a language with a garbage collector

- **The vtable must have static storage duration and must never move.** The Grid holds the pointer
  for the whole run and reads it after `library_shutdown` has been called. In Go this means the table
  lives in C memory rather than Go memory, because cgo forbids storing a Go pointer where C can keep
  it.
- **Borrowed pointers must be copied, never retained.** Every pointer in `TglSenses` is valid for the
  duration of one `program_tick` call and invalid afterwards. Wrapping one in a slice, a view or an
  array object that outlives the call is the same defect as keeping the raw pointer, and it will not
  fail immediately.

A language with a runtime — Go, Haskell, OCaml, Swift — starts that runtime inside the library, which
is fine, but `program_tick` runs on the Grid's tick thread and must not block. A garbage collection
pause inside a tick is a tick the whole Grid waits for.

---

*See also: `docs/PERCEPTION.md` for the sensor model, `docs/DEV_ENV_SETUP.md` for building the
Grid, and `STYLE.md` for code style.*
