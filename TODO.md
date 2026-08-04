# TODO

The live source of truth for TronGrid Lite work. New work is added as etapes and criteria are ticked
when satisfied; a finished etape collapses to one line, because what it *decided* belongs in
`CHANGELOG.md` and what it *built* belongs in the code with the reasoning attached.

## Roadmap (phases)

| Phase | Goal                          | Milestone                          | Status |
|-------|-------------------------------|------------------------------------|--------|
| 0     | Prove the toolchain           | Triangle on screen                 | **Done** |
| 1     | Window, swapchain, frame loop | Fly through a wireframe grid       | **Done** |
| 2     | BVH + primary rays in compute | Mirror world, first bounce         | **Done** |
| 3     | Full ray tree                 | Reflections, emissives, glass      | **Done** |
| 4     | Post processing               | Bloom, tonemapping                 | **Done** |
| 5     | Acoustic rays                 | Echoes and occlusion via same BVH  | **Done** |
| 6     | Programs                      | Creature sensor interface plugs in | Pending |

## Completed etapes

Eleven etapes, all boxes ticked. They are collapsed to one line each because a finished checklist is
not a plan — what each one *decided* lives in `CHANGELOG.md`, and what each one
*built* lives in the code with the reasoning attached to it. Keeping the checklists as well meant
maintaining a third copy that drifts.

| # | Etape | Delivered |
|---|-------|-----------|
| 1 | Adopt project infrastructure from TronGrid | Linting, governance, CI, and the six internal libraries with their tests |
| 2 | Phase 0: triangle on screen | Instance, device, surface, swapchain, dynamic rendering |
| 3 | Phase 1: window, swapchain and frame loop | User camera, neon grid, GPU timestamp profiling |
| 4 | Phase 2: the compute ray tracer | Host BVH in storage buffers, compute traversal, rasteriser retired |
| 5 | Phase 3: the full ray tree | Transmission, Snell refraction, total internal reflection |
| 6 | Phase 4: post processing | HDR target, bloom chain, fitted ACES curve |
| 7 | Phase 5: acoustic rays | Sound sources, the gather on host and device, ears in the ABI |
| 8 | Move rendering onto its own thread | Render thread, `Window::wakeEvents`, on-demand drawing |
| 9 | Phase 6 prerequisite: sub-allocate device memory | `MemoryArena`; sub-allocation warnings 16 to 2 |
| 10 | Phase 6 prerequisite: a two-level hierarchy | `Scene`, `Instance`, `flatten`; `traceScene` in the shared module; both senses on it |
| 11 | Make the Grid a command-line program that can open a window | Null-surface `Device`, `--window`, every other mode headless |

Decisions from those etapes that are load-bearing enough to live in the code rather than here, and
are worth knowing before touching the areas they govern:

- **An arena block is mapped once, by the arena.** Vulkan forbids mapping one `VkDeviceMemory` twice,
  so buffers sharing a host-visible block cannot each map it. `MemoryArena::bind` returns the address
  for that reason. Phase 6's per-creature buffers will meet this.
- **Staging buffers are deliberately not sub-allocated.** Each exists for one copy and is destroyed
  before `uploadStorageBuffer` returns; the validation layer's advice is simply wrong for a one-shot
  transfer scratch. That is why two warnings remain and should stay.
- **A ray transformed into instance space is not normalised**, in `BvhLib::intersectScene` and in
  `grid_bvh.slang` alike. Leaving it alone makes the ray parameter identical in both frames, so a
  distance found in instance space is already a world distance. It looks untidy, it is load-bearing,
  and tidying it breaks two tests on purpose.
- **The Grid is an instance at the identity, not a special case.** The path a creature body will take
  is the path the only body in the world takes today, so it is exercised by every frame rather than by
  a test written for one instance and a comment promising the rest.
- **With `--window` there are no creatures.** The window is a debug view of the Grid so the User can
  check things are in their place, not a viewport onto a running simulation. That is what licenses the
  on-demand gate to skip whole frames while somebody drags a window edge: nothing alive can miss a
  tick, because the tick and the swapchain never coexist.
- **The reference render digests are the only genuine one-way door here.** Regenerating the two rows
  in `.claude/CLAUDE.md` needs physical access to both GPUs. Never move a digest speculatively, and
  batch anything that must move it into a single commit.
- **What the digest cannot see.** It is a check on a picture produced by a single-threaded path, so a
  lock inversion, a lost wakeup or a join waiting behind `acquireNextImage` all leave it green — and
  ThreadSanitizer does not close that gap, because it proves the absence of *races* and says nothing
  about order. Anything touching threading needs its own check, and a threading refactor is exactly
  where a green digest is least informative and feels most reassuring.

## Etape 11 — Import creature bodies as glTF

**Not now, and this entry exists so that "not now" is a decision rather than a gap.**

Creature bodies will be modelled in Blender rather than generated procedurally as the Grid's own
furniture is, so the Grid needs to read a mesh file. **glTF 2.0 is the right format** and there is no
serious competition: it is the Khronos standard, Blender exports it natively with no plugin, and it
is the only interchange format that is both openly specified and actually ubiquitous. OBJ carries no
transforms or hierarchy; FBX is proprietary; USD is enormous.

Decisions worth pinning now, while they are cheap:

- **`.glb`, not `.gltf`.** The binary container is a single self-contained file. The JSON form
  references external buffers and images by URI, which means path resolution, relative-path rules and
  a class of "works on my machine" failures for no benefit here.
- **A very small subset.** Positions, indices, and node transforms. That is the whole of it, and it
  is a small fraction of the format.
- **No materials are imported, ever.** A creature's optical properties are the Grid's business, not
  Blender's — the Grid has a four-float material model with no textures, and a glTF PBR material has
  no way to express it and several ways to be misread. A body arrives as geometry and is assigned a
  `MaterialSlot` on this side. The same goes doubly for the acoustic table, which Blender cannot know
  anything about.
- **The work is the JSON, not the glTF.** The mesh extraction is a few dozen lines of accessor
  arithmetic; parsing JSON with no dependency is the bulk. That is worth knowing before estimating it.
- **Two formats, never one.** A body is a `.glb` the Grid reads and never authors. The world, if it
  ever becomes a file, is a small manifest the Grid owns outright, and the manifest names bodies
  rather than the reverse. **No load-bearing data in glTF `extras`**, which is schemaless,
  unvalidated, and has known Blender round-trip defects.
- **The Grid itself is never baked to a mesh.** Its floor is an analytic height function, and
  `gridSurfaceHeight` and `gridMeshHeight` are what floor-planting and ear placement stand on. Baking
  deletes both, makes every ground query a downward ray, and trades roughly 1.5 KB of parameters for
  1.14 MiB of triangles. Of the Grid's 24,952 triangles only 120 — under half a per cent — correspond
  to something a human placed; the rest come from about eleven numbers.

**Two defects in the official Khronos sample loader, recorded now because they are free to avoid and
expensive to find later.** The reference `.cpp` the tutorial ships gets both wrong, so copying it is
worse than starting from the specification:

- **Read `bufferView.byteStride`; never hardcode the element step.** The sample uses `i * 12` for
  positions and `i * 8` for texcoords and ignores stride entirely, which corrupts any interleaved
  `.glb` — and interleaved is what Blender and `gltfpack` routinely produce. The correct address is
  `bufferView.byteOffset + accessor.byteOffset + i * stride`, and both offsets add.
- **Never bake a Y-flip into vertex positions.** The sample writes `{pos[0], -pos[1], pos[2]}`, which
  mirrors the geometry, inverts winding and normals, and is mathematically inconsistent with applying
  node transforms — those were authored in unflipped space. The sample escapes the consequence only
  because it ignores node transforms; a loader that honours them, which this one must, will not.
  Handle the coordinate convention once, in a root matrix.

The scene-graph recipe worth taking is in a different chapter from the one a reader would look in: a
flat `std::vector<Node>` addressed by index, loaded depth-first so parents precede children, with
world matrices resolved in one forward linear pass — no recursion and no sort.

- [ ] `libs/gltf` — a `.glb` reader for positions, indices and node transforms

**Two questions to answer before writing a line of it**, because both could change the shape:

1. **Skinning — now decidable, and the convergence is the uncomfortable part.** Three independent
   lines point the same way: the two-level hierarchy is nearly free for rigid segments and merely much
   better for skinned ones; a world of flat-shaded facets has no use for smooth deformation; and a body
   built as a chain of rigid segments in contact with the floor is what the acoustic rule requires
   anyway, because it is stick-slip that makes a scrape modulated rather than steady. Nothing argues
   the other way except that the published creature-locomotion field also settled on rigid segments,
   which is agreement rather than evidence.

   Closing it in favour of rigid segments deletes `skins`, inverse bind matrices and per-vertex joint
   weights from the subset above, and shapes the loader. Leaving it open leaves the loader unshaped,
   which is the more expensive of the two states to sit in.
2. **What it does to the hierarchy — answered and built.** Two-level, on the host and in the shader,
   with the measurements in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) § One hierarchy today, two
   when creatures move. A body arrives as a geometry and a transform, and nothing rebuilds.

   It does leave the first question sharper than it was. **Rigid segments make the two-level structure
   nearly free, because a rigid body's hierarchy is built once; skinning makes it merely much
   better**, since a deforming body must rebuild its own — about 0.45 ms per thousand triangles, so
   roughly 9 ms for twenty creatures. A world of flat-shaded facets has little use for smooth
   skinning, so the cheap answer and the fitting answer coincide, which is worth being slightly
   suspicious of.

## Etape 12 — Phase 6 prerequisite: confine Program library paths

**A security obligation that is not yet due, recorded here because it becomes due at a specific and
foreseeable moment.**

Code scanning has repeatedly flagged paths reaching the filesystem from the command line, and every
one of those alerts has been dismissed as a false positive. The reasoning is sound *today*: the only
party who can set `--output` or `--gif` is the person who already owns the process and could do the
same with a shell redirect. The query assumes `argv` is attacker-controlled, which is right for a
setuid binary or a service and wrong for a renderer somebody launches themselves.

**That reasoning expires in Phase 6.** Once the creature roster resolves Program library paths out of
a config file, as `docs/PROGRAM_INTERFACE.md` describes, the path stops coming from the command line
and starts coming from a file that a downloaded creature pack could have written. At that point the
input genuinely is untrusted, the query is right, and confinement becomes a requirement rather than
theatre.

- [ ] Confine Program library paths before the Grid loads a Program from a config file

Two dead ends already explored, so that nobody spends the afternoon again:

- **Validating the path does not clear the query.** It recognises particular normalise-and-contain
  barriers, not hand-rolled checks — this was tried on `tools/record_flyby.py` and the alert
  survived while a second one appeared.
- **A `query-filters` exclusion would be silently inert.** There is no CodeQL workflow in this
  repository; scanning runs under GitHub's default setup, which never reads that file. Making it
  work would mean owning a hand-maintained workflow forever and disarming the query repository-wide.

The fix that did work on the recorder was removing the capability rather than guarding it: `--preset`
and `--config` name choices from constant tuples, so no path comes from `argv` at all. The same shape
is the likely answer here — a Program identifier resolved against a known directory, rather than a
path taken on trust.

**One sequencing point, because it is a real decision and not a preference.** The Program loader is
not merely the thing that arrives *near* this etape — it **is** the untrusted-path code this etape
exists to guard. So there are only two orders: confinement lands first and the loader is written
against it, or the two land in one commit. "Loader now, confinement soon" is the one order that
cannot be chosen, because it means shipping the exact code the alert describes while the dismissal
that covered it has already expired.

## Etape 13 — Phase 6: questions to settle before writing the tick loop

The design is settled; these are the places it is settled *conditionally*, and each one changes the
shape of the code rather than a constant in it. They are here rather than in a document because they
are decisions waiting on the owner, not conclusions.

1. **Does a live Program clock the Grid unconditionally, or may the Grid infer quiescence from zero
   actions over K ticks?** There is no third option — an ABI field by which a Program declares itself
   idle is cognition leaking into the Grid. Inference costs a creature that would have moved on tick
   N+1 the tick N+1 in which to choose it. Unconditional ticking costs the CPU floor, which is a
   number and should be quoted as one.
2. **Does the world tick share the render thread, or get its own?** Sharing it for v1 keeps the render
   channel's audit comment true — *if a name is not a member of this struct, exactly one thread
   touches it* — and keeps the mapped instance array from becoming shared mutable state across three
   parties.
3. **Tick rate: 25, 32, 50 or 100 Hz.** 32 Hz gives `dt = 0.03125` and a substep of `0.00390625`,
   both exact in float32, so `tick * dt` is exact for 145 hours. 25 Hz gives 0.04, which is not exact
   and accumulates a drift somebody eventually has to explain in a recording — and is the only rate
   implied anywhere in this repository today. 50 Hz is the middle and puts a four-substep tick at the
   5 ms where published humanoid models sit. The rate must never be sized to what looks smooth
   through the debug window; that window is the only part a human can perceive, which is exactly why
   it must not drive the decision.
4. **Is world replay claimed at all?** Claiming it costs single-threaded host physics permanently —
   cheap now, a constraint later. Not claiming it costs nothing today and invites somebody to assume
   it.
5. **Does the first creature body have geometry?** The largest single scope decision here. Without it,
   physics is still verifiable, the generation never moves, and `World` needs no change at all in v1.
   With it, the debug window shows the creature and the writable instance buffer, the frames-in-flight
   write, the per-tick `flatten` trap and runtime rez all come due at once.
6. **How many segments, and does the first worm get its two eyes?** Segment count is action-space
   dimension only when there is per-segment drive, and v1 has none, so eight segments cost nothing but
   instance count. Two one-sample eyes make the creature testable against the one documented behaviour
   in the literature; zero make it cheaper and untestable against anything.
7. **Roster size, bounded by the top-level sweep rather than by physics.** Four bodies stays
   comfortably inside the regime `intersectScene`'s linear sweep was measured in. Twenty bodies of
   eight segments is 161 instances, and with two eyes each that is tens of millions of instance-box
   tests per tick — which forces a top-level hierarchy now rather than later.
8. **Is the per-segment-drive ABI break made now on principle, or later on evidence?** Deferring is
   defensible while no Program exists to demonstrate the need. The counter-argument is that the break
   is cheaper today than after the first Program ships, and every published creature environment feeds
   per-joint state back as observation.
9. **Does `desired_vertical_speed` survive into the written header?** On a Grid with no water, nothing
   climbable and nothing to fall off, it clamps to zero for every plausible v1 body, and it is the one
   action field with no proprioceptive counterpart. Retiring it now is the honest reading of *every
   field is populated*; keeping it and marking it reserved avoids a second break later.
10. **Is the physics hierarchy the render hierarchy?** The neon tubes stand proud of the floor in the
    one hierarchy that already serves both senses, and letting them collide is what makes the lattice
    something a creature can feel and count. The alternative is a per-triangle collidable flag in
    `Triangle::padding0`, which is already in the std430 layout, already uploaded and already
    asserted, so it is free in bytes. One Grid two senses, or one Grid three — decide it rather than
    discover it.
11. **Does the sample-list eye ship with one ray per sample?** One ray per sample delivers the
    aliasing that [docs/PERCEPTION.md](docs/PERCEPTION.md) rule 4 wants and not the acceptance-angle
    blur rule 6 wants. A scope decision, not an engineering one.
12. **Is hot reload repriced?** In-process loading was chosen for speed with the crash cost accepted,
    but the cost actually paid every evening is that changing one line of a Program means restarting
    the Grid, rebuilding the hierarchy and flying the camera back. If derez-all, unload, reload,
    rez-all works behind a keypress it is probably the highest-value ergonomic feature in Phase 6; if
    it cannot, that deserves to be a stated conclusion rather than a one-line *Not supported*.
13. **Two names, cheap now and expensive later.** `spectator` is retired vocabulary and
    `SpectatorController` still carries it; any restructuring commit is the cheapest moment that
    rename will ever cost. And if a type ever owns the tracer and the post-process together, is
    `Renderer` too grand for something that owns no device, no swapchain and no camera?

One piece of work is not a question and blocks everything: **the ABI header does not exist.**
`TGL_PROGRAM_ABI_VERSION`, `TglSenses` and `tglGetProgramVTable` are Markdown prose with no compiled
existence anywhere in the tree. The header and a Program returning constants ship **before** physics —
the phase milestone is that the sensor interface plugs in, and physics is what makes the senses stop
being constant afterwards.

- [ ] `libs/program-abi` — the C99 header, an `INTERFACE` target, vendorable into other trees
- [ ] Load a Program: `LoadLibrary`/`dlopen`, one symbol, version refusal, vtable, lifecycle
- [ ] One body with no physics — a transform that changes between ticks
- [ ] Fill the senses from the tracers that already exist
- [ ] Physics: gravity, contacts, friction
- [ ] A remote-operated demo Program with its own telemetry GUI

### Thirteen defects in `docs/PROGRAM_INTERFACE.md`, found by writing the header against it

The document is the specification and it does not currently compile as one. Correcting it is part of
writing the header, not separate from it.

The fatal one: **the version constant cannot do the job it is kept for.** It is `1u` and documented to
stay `1u` forever, so a stale library compiled against an older header also carries `1u`, passes the
check, returns a vtable, and produces exactly the memory corruption the paragraph describes. The fix
is a constant that bumps on any change an already-built Program could notice, plus a CI step that
hashes the header with the version line removed and fails when the hash moves and the number does not
— a mechanism rather than a discipline.

The rest, in brief: the lifecycle puts *advance physics* and *apply actions* both inside the
per-creature loop, which makes roster order semantically load-bearing and "actions take effect next
tick" false; threading promises a single tick thread and parallel creature ticks one bullet apart;
the struct listings a reader is told to transcribe omit the ear members entirely; `TglEyeDesc` has no
position or optical axis where `TglEarDesc` has both; `TGL_EYE_LAYOUT_RASTER` has zero users among the
six presets and samples solid angle backwards for a foveated eye; `TglEyeDesc` omits the per-sample
acceptance angle and quantisation that PERCEPTION.md rule 3 requires; the acoustic scale has two
reference levels and one of them names something that does not exist; `irradiance` as specified is a
single-sample Monte Carlo estimate, which the determinism rule forbids; the ear histogram's span is
undefined and two documents disagree about it, 16 bins against the 64 the code delivers; the clamps
are promised seven times against limits that appear in no struct; `TglEarDesc::direction` is read by
nothing; and the tick rate is spelled three different ways.

## Etape 14 — Name every Vulkan object, and handle a lost device once

**Not now, and this entry exists so that "not now" is a decision rather than a gap.**

Three things travel together and none of them exists here: names on every Vulkan object through
`VK_EXT_debug_utils`, labelled command-buffer regions, and one central handler for
`VK_ERROR_DEVICE_LOST` that stops submitting, flushes what it knows, writes it down and exits —
because continuing after a lost device is undefined. It is one investment with three payoffs:
validation messages that name the object rather than a handle, RenderDoc captures that read like the
pass list, and vendor crash dumps that point somewhere.

The reason to record it rather than forget it is that this repository has already lost a device once,
to a descriptor array resized without gaining its new entries, and the only thing that named the fault
was reading the code afterwards. Phase 6 multiplies the object count by the roster.

- [ ] Debug-utils names on every Vulkan object, labelled command-buffer regions, one `DEVICE_LOST` path

## Etape 15 — Use exceptions only where they are necessary

The policy: **accept what the Vulkan SDK throws, throw our own only from constructors that have no
other channel, and never let either reach `std::abort` or the Program ABI.**

`vk::raii` throws and the swapchain path relies on catching `vk::OutOfDateKHRError` at the call site,
so exceptions exist in this program regardless. What is in our gift is how many of them are ours, and
the answer should be as few as a constructor genuinely needs.

Three of our throw sites are in constructors and stay. Fifteen are not, and fall into three groups
that want three different answers rather than one rule:

- **Programmer errors, 5** — `AcousticTracer::setEars`, `::record` twice, `::read`, and
  `PostProcess::resize`. Every caller is our own code passing a count it already knows. These are not
  errors at all; they are either assertions or a signature that cannot express the mistake.
- **Environment failures, 7** — `readSpirv` four times, `findMemoryType`, and `writePpm` twice. A
  missing or truncated file is something a user can cause, and a free function can return a
  `std::optional` or a `bool` without any of the machinery a constructor would need.
- **Argument validation, 2** — the recording and benchmark extent checks, which belong in the parser
  beside the other argument handling rather than in the function that consumes the value.

- [ ] Convert the fifteen non-constructor throws, by group
- [ ] Convert the fifteen `std::abort` sites in `device.cpp`, `instance.cpp`, `swapchain.cpp` and the
      three window backends

**The aborts are the defect, not the throws.** `logFatal` flushes and writes to stderr before them, so
the diagnosis survives — but the process terminates abnormally rather than returning `EXIT_FAILURE`,
no destructor runs, and on Windows the CRT may raise its termination dialog: a modal box on somebody's
screen, and a hung job on a machine with nobody in front of it. This repository has met that failure
once already, from a debug assertion. A constructor that throws is the cleanest failure C++ offers —
the object never existed, and every member already built is destroyed in reverse order by the language
itself.

## Etape 16 — A check that runs without a GPU

Every check this repository has that could catch a rendering or acoustic regression needs a device,
so all three fire only when somebody remembers them on a machine with one attached. Two things would
change that, and neither needs the MMO or the server to exist first.

- [ ] A per-tick physics state hash: N ticks from a fixed roster and a fixed action stream, hashing
      every body. It needs no device, so it runs under `ctest` on every push. `src/tests/` already
      compiles renderer translation units into GPU-free targets and CI already runs `ctest` on
      `ubuntu-latest`, so the mechanism exists before the subsystem does. It would be the first
      determinism check here that fires without being remembered.
- [ ] Evaluate lavapipe — Mesa's software Vulkan — as a CI device. If it runs the compute path,
      `--verify-acoustics` and `--verify-scene` become CI checks rather than rituals. The digests
      would not match a real GPU and are not the point; the host-against-device comparison is.

A headless server, when one exists, is `libs/bvh` plus `libs/math` plus a tick loop with **zero device
code** — which makes it the first substantial part of this project testable on any runner at all.

## Where the research went

Nine studies ran across 2026-08-03 and 04 — determinism, threading and SignalsLib, engine structure,
the tick loop, the original TronGrid's command-line duality, creature bodies, the Program ABI, stiff
jointed bodies, and Grid-as-data. Their briefs are archived outside this repository, in
`AiQuokkaWannabe/research-archive/`, because they are working notes rather than project documents and
would bloat the tree.

The durable conclusions were extracted from them and routed into the files that own them; the archive
keeps the reasoning behind each. Where a conclusion contradicted a document, the document was
corrected rather than appended to.

## Where the history went

There was a journal here. It is gone, and deliberately, because it was a third copy of things that
already had two homes and it grew faster than either.

- **What changed and why** lives in [CHANGELOG.md](CHANGELOG.md), which carried nearly all of it
  already.
- **Rules worth obeying next time** live in [.claude/CLAUDE.md](.claude/CLAUDE.md) § Hard-won rules,
  condensed to imperatives. A lesson written as a story is read once; written as a rule it is read
  every session.
- **Everything else** was narrative, and the durable facts inside it were already in the code they
  govern — the positive-infinity miss sentinel is explained in both `libs/bvh` and `grid_bvh.slang`,
  the `std::from_chars` argument parsing in `main.cpp`, the rejected compass in
  `docs/PROGRAM_INTERFACE.md`. That is the test applied before deleting any of it: a fact may leave
  this file when it lives where it is enforced.

One thing did not survive that test and was promoted rather than deleted — the Phase 6 path
confinement obligation, now Etape 12 above. It existed nowhere else.
