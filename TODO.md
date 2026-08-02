# TODO

The live source of truth for TronGrid Lite work. New work is added as etapes;
criteria are ticked when satisfied; the Journal records what actually happened.

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

Nine etapes, all boxes ticked. They are collapsed to one line each because a finished checklist is
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

Two decisions from those etapes are load-bearing enough that they live in the code rather than here,
and are worth knowing before touching either area:

- **An arena block is mapped once, by the arena.** Vulkan forbids mapping one `VkDeviceMemory` twice,
  so buffers sharing a host-visible block cannot each map it. `MemoryArena::bind` returns the address
  for that reason. Phase 6's per-creature buffers will meet this.
- **Staging buffers are deliberately not sub-allocated.** Each exists for one copy and is destroyed
  before `uploadStorageBuffer` returns; the validation layer's advice is simply wrong for a one-shot
  transfer scratch. That is why two warnings remain and should stay.

## Etape 10 — Phase 6 prerequisite: parallelise the hierarchy build

**Deliberately not done yet, and this entry records why, so that the deferral is a decision rather
than an oversight.**

`BvhLib::build` is single-threaded and takes **14 ms** for the Grid's 24,952 triangles on a 16-core
machine — about 2 % of a 650 ms startup, nearly all of which is Vulkan device and pipeline creation
that no amount of threading can touch. Parallelising it today would buy roughly 10 ms of startup in
exchange for a concurrent node allocator, against a repo rule that says don't over-engineer.

An earlier plan called this urgent on the strength of a 120 ms measurement. That was a Debug build;
see the 2026-08-02 journal entry.

**Phase 6 is where it changes**, because a hierarchy over moving creatures is rebuilt every tick
rather than once at startup, and 14 ms against a frame budget is a different proposition entirely
from 14 ms paid once at launch.

- [ ] Parallelise the hierarchy build before creatures start moving

**Read Etape 11's second question first.** This etape assumes the thing being rebuilt every tick is
the one hierarchy over the whole Grid. If bodies get their own small structures under a tiny top
level instead, the Grid's own hierarchy never changes at all and the per-body ones are trivial to
build — in which case this etape is not a smaller problem, it is the wrong problem.

The shape is already half-decided by the existing code. `subdivide` partitions its range before it
recurses, so the two children own disjoint triangle ranges and never touch each other's — the only
shared mutable state is `nodes`, which both children append to. The determinism requirement is what
rules out the obvious atomic bump allocator: node indices would then depend on thread scheduling.
Building each subtree into its own local vector and splicing them back in a fixed frontier order
keeps the output bit-identical between runs, which a reproducible recording requires.

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

- [ ] `libs/gltf` — a `.glb` reader for positions, indices and node transforms

**Two questions to answer before writing a line of it**, because both could change the shape:

1. **Skinning.** A creature that walks has joints. glTF expresses that with `skins`, inverse bind
   matrices and per-vertex joint weights, and supporting it is a large step up from static meshes. If
   bodies are rigid segments connected by the Grid's own physics rather than skinned meshes, none of
   it is needed — and rigid segments are the likelier answer for a world whose whole aesthetic is
   flat-shaded facets.
2. **What it does to the hierarchy.** This is the sharper one, and it reaches back into Etape 10. The
   Grid's BVH is built once over static geometry. A moving body cannot live in it without a rebuild
   every tick, which is the wrong shape: the standard answer is two-level, a static Grid structure and
   one small structure per body with a tiny top level over them. **That is a decision that belongs
   before parallelising the single-level build, not after**, because it may make the parallel build
   unnecessary — a per-body structure over a few hundred triangles is trivial to rebuild, and the
   Grid's own never changes.

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
