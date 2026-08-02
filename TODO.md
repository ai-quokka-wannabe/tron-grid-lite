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
not a plan — what each one *decided* lives in `CHANGELOG.md` and the journal below, and what each one
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

## Journal

### 2026-08-03 (second hunt, libs and docs)

Pointed the three questions at `libs/`, which had barely been audited, and pruned the completed
etapes while there.

- **The worst find was a safety property that did not exist.** `ARCHITECTURE.md`'s design-decision
  table said `Signal<T>` was "lifetime-safe via `weak_ptr`". The class is a mutex and a queue; the
  `weak_ptr` text was an ownership convention in a comment, and *neither user adopts it* — both hold
  the queue as a plain member. A table entry asserting safety that is not implemented is worse than
  no entry, because it stops the reader thinking about lifetime at all.
- The stated rule that each library has an `include/<lib>/` directory is false for two of six:
  `signals` publishes `signal/`, `logging` publishes `log/`.
- `ACOUSTICS.md` still cited "the current TODO item, `Fill hearing_samples`" — an item removed some
  time ago. The argument it supported survives; the citation did not.
- Several claims **checked out**, which is worth recording so nobody re-checks them: the library
  target types (two `INTERFACE`, four `STATIC`), the six `*Lib` namespaces, the advertised test
  macros, the BVH's `2n - 1` node bound, and the Logger's fatal path flushing before it writes to
  stderr. The Logger also emits under the waiter's mutex — which is where the render channel's
  version of that fix came from in the first place.

**Pruning.** Nine completed etapes became a one-line table. A finished checklist is not a plan, and
keeping it beside the changelog and the journal was a third copy to drift. Two decisions were rescued
from the collapse because they still constrain future work — the once-only arena mapping and the
deliberate non-sub-allocation of staging buffers — and both were already in the code they govern,
which is the test for whether an etape can safely go.

**A note on the sweep itself.** Grepping for removed symbols across the repository returned mostly
`.pdb` files out of `build/`. Excluding build output is the difference between a useful sweep and
twelve false positives.

### 2026-08-03 (bug hunt)

A deliberate hunt rather than a reaction to anything breaking. Twelve findings, and the two worth
remembering are both **silent** failures — no crash, no validation error, no test going red.

- **The acoustic histogram was read without a host-visibility barrier.** A fence proves the work
  finished; it does not make shader writes available to the host domain, and host-coherent memory
  removes the need to *invalidate* rather than the need for the dependency. `recordCinematic` has done
  it correctly since Phase 4, forty lines from the new code. It worked on this driver, which is the
  weakest evidence there is.
- **The ABI said bin-major; every implementation is band-major.** Four bands by sixty-four bins does
  not crash when transposed — it produces plausible nonsense in whichever Program trusted the
  documentation. Caught by comparing the doc's index expression against the two that exist.
- **`MAX_DEPTH` was tied across the host/shader boundary by a comment.** Raise the host's and the
  shader silently *drops* far children it cannot stack, losing hits with no diagnostic. Now asserted,
  and the assert was mutation-tested. The same gap existed for three acoustic constants.
- The fixed-point overflow guard assumed unit amplitude when both inputs are caller-supplied, and
  `ACOUSTICS.md` was an order out on the same arithmetic — a ray makes `max_order + 1` segments, not
  `max_order`.
- Assorted stale claims, including `README.md` asserting that the embedded clip "is the current
  output" when it predates the terraced floor.

**What actually found them.** Not reading code top to bottom. Three specific questions, each of which
had already caught something once before:

1. *Where does this cross a boundary the compiler cannot see?* — host/shader constants, host/device
   memory visibility, the ABI's layout against the code's.
2. *Where does a document state a number I could check?* — the deposit count, the warning count, the
   phase status.
3. *Where does an existing part of this repo already do the same thing?* — the cinematic readback had
   the barrier the acoustic pass lacked, and `BAND_COUNT` had the assert `MAX_DEPTH` lacked.

The pattern behind the first is worth stating: **every one of the serious findings was a duplicated
fact with no mechanism holding the copies together.** Comments saying "must equal" are not mechanisms.

### 2026-08-03

- **The acoustic pass runs on the GPU.** `acoustics.slang` imports the same `grid_bvh` module the
  renderer uses and binds the same two `World` buffers. One workgroup per ear, histogram in shared
  memory as `uint`s. The integer accumulation is not a workaround for missing float atomics — it is
  better than them here, because integer addition is associative and the histogram therefore comes
  out bit-identical however the threads are scheduled.
- **`--verify-acoustics` earned its keep on its first run**, reporting a 1.2 % disagreement at one
  ear and 0.003 % at the other. That asymmetry is what made it obviously a bug rather than noise.

  Three diagnostics, two of which were wrong, and the order matters:
    1. A reflection-order sweep showed the disagreement fully present at **order zero**. Not bounce
       instability.
    2. Nudging the ear by up to 100 µm moved the host total by 1.6e-5 — smooth, no jump — which
       looked like proof that nothing was knife-edge. **That test was too weak**: 100 µm only flips
       rays already within 100 µm of a tube edge, so it could not see the one ray that mattered.
    3. Running the host's BVH against the host's own brute-force sweep gave delta **exactly zero**.
       That acquitted the traversal and left only one shared suspect: the direction set.

  The cause was `golden_angle * ray_index` reaching ~4,913 radians at index 2047. Float32 carries
  ~6e-4 rad of error at that magnitude, and host and device reduce it differently — two millimetres
  of displacement at three metres, enough for one ray in 2,048 to hit a 2 cm tube on one side and
  miss on the other. The entire 1.2 % was that single arrival in a single bin.

  Both sides now accumulate the turn fraction and reduce before the trigonometry. Every step is an
  operation IEEE-754 specifies exactly, so the arguments are bit-identical and `cos` never sees more
  than one turn. **The lesson to keep: when two implementations disagree, find the thing they share
  and the thing they do not, rather than reaching for "floats differ" — which explains everything and
  therefore nothing.**
- Rounding rather than truncating the fixed-point deposit removed a systematic deficit worth another
  factor of eighty. Final agreement: **0.00005 %** of the total, worst single bin 0.00004 %.
- Verification thresholds were then tightened from 1 % and 5 % to 0.1 % and 0.5 %, set from the
  measurement rather than from taste. At the old values the next bug of this kind would have hidden
  exactly as thoroughly as this one did.

### 2026-08-02 (late)

- A post-merge audit of the acoustics work, prompted by nothing breaking — which is the point. Four
  defects, none of which any test or build had complained about:
    - **An out-of-range material index was undefined behaviour** in the gather. Fixed with one
      compare per hit and pinned by a test. Confirmed by mutation, which is how the next item was
      found.
    - **`makeMaterials` was sized by a literal `6u`** while the acoustic table used
      `MATERIAL_SLOT_COUNT`. Adding a slot would have silently left the optical table short.
    - **A failed CRT assertion opened a modal dialog** — discovered by popping one on Matej's screen
      during that mutation test, which is a poor way to find it but a real find. In CI this is a
      hang rather than a failure: nobody clicks the box, the runner times out, and the log never
      says which test it was. `TestingLib::runAll` now routes CRT reports to stderr.
    - **Stale attributions** still crediting `trace.slang` with declaring `Node` and `Triangle`.
- The `_DEBUG` guard on that last fix was itself found by the compiler: outside a debug CRT the
  `_CrtSetReportMode` calls are macros expanding to nothing, so the loop variable went unread and
  `/WX` rejected it. Worth recording because it is the good case — the build catching a mistake in a
  fix for something the build could not catch.

### 2026-08-02 (evening)

- **Every acoustic surface is now a perfect mirror.** Absorption removed, transmission stated as a
  decision rather than a deferral. `acoustics.cpp` 225 → 122 lines, and the ray payload lost its
  throughput scalar — it carries accumulated path length and nothing else, which makes the trace
  fully band-agnostic and so lets one gather serve any listener's band edges.
- `airAbsorptionDbPerKm` went too, and with it an extrapolation above 8 kHz I had flagged as
  untrustworthy the same day I wrote it. Per-band air absorption is authored per listener beside the
  band edges from the same audiogram. Nothing simulates air.
- Recorded the rule that **nothing on the Grid sounds continuously**. The compute saving is smaller
  than it looks — the gather was already cacheable, being a pure function — but the perceptual reason
  is decisive: a continuous tone carries almost no delay information, since every arrival overlaps
  every other. Onsets are what make a delay measurable. The worm scrape falls out correctly as easy to
  detect and hard to range, which nobody designed in.
- **A test that had never tested what it claimed.** The parallel-plate scene used a 200 m sounding
  floor, so a shallow *direct* ray reached fifteen metres exactly as the ceiling echo did — the
  "reflected arrival" assertion was satisfied by direct rays and proved nothing about reflection. The
  sounding patch is now 3 m, which separates direct (bins 14–19) from reflected (43–45) by arithmetic,
  and the tests now assert the gap between them is empty.
- **A docs/code disagreement found while rewriting.** `ACOUSTICS.md` gave spreading as `1/(4πd²)`;
  the code implements `1/max(d², 1)`. Both departures are deliberate and neither was written down —
  the `4π` folds into a source scale that is relative by construction, and the one-metre floor is the
  Grid's own unit and is what makes "no arrival exceeds its source strength" a checkable invariant.
- **The documentation debt is paid.** `ACOUSTICS.md` had carried a checklist of nine documents
  describing states the code had left behind; eight were already fixed and the last two — `VISION.md`
  claiming sound passes through glass, and `README.md` principle 5 — are done. Its correction to
  `PERCEPTION.md` is applied in both places it named. The checklist is kept as a record of the
  pattern: every entry was a document confidently asserting a *layout* it did not own, and unlike code
  a stale layout claim does not fail to compile.

### 2026-08-02 (afternoon)

- **Phase 5 has a model.** The CPU gather, the acoustic material table, the ISO 9613-2 air absorption
  row and eight tests. This is the specification; `acoustics.slang` mirrors it next, exactly as
  `trace.slang` mirrors `libs/bvh`.
- Two extractions came first, and both were verified as no-ops by hashing eight recorded frames
  before and after: `grid_bvh.slang` for the traversal both senses share, and `World` for the Grid's
  geometry on the device. `libs/bvh` has claimed since it was written that "the same hierarchy is
  intended to serve acoustic rays later, which is why nothing here is specific to light" — those two
  are where the claim is actually kept, on the device and on the host.
- **Nothing runs without a reason.** The renderer was spinning at 270 fps redrawing an identical
  picture of a Grid that cannot yet change. It now compares the state it drew from and sleeps on the
  render channel's condition variable: **0.09 s of CPU over 12 s against 7.56 s drawing every pass**,
  roughly eighty times less. State comparison rather than dirty flags, because a flag is only correct if
  every writer remembers it. One hazard found while building it and fixed: a swapchain rebuild leaves
  images with undefined contents, so a resize that happened to leave the camera and size unchanged
  would have looked idle and left garbage on screen — a rebuild now forces the next frame.
- The acoustic side gets the same rule and a stronger justification. `gather` is a pure function, so
  a solve whose inputs have not changed may be skipped and the skipped answer *is* the right answer
  rather than an approximation of it. The renderer has to compare state because floating-point camera
  integration can wander; a gather cannot. `src/tests/acoustics_tests.cpp` pins the cache key by
  showing each of its three parts can change the answer.
- A test asserted something false about the geometry and had to be corrected rather than the code: I
  claimed nothing could arrive in bin 13 at an ear two metres up, forgetting that a slanted ray
  reaches the floor at 4.46 m. Replaced with what was actually meant — the first occupied bin is the
  perpendicular drop, and a lower ear hears the floor sooner.

### 2026-08-02

- Etape 8 done: the renderer runs on its own thread. See the etape above for what was built and for
  the two things the plan did not anticipate.
- **Every performance figure recorded in this project so far was measured with the validation layers
  on, and they are all roughly 4.6× too slow.** Debug enables both core validation and *GPU-assisted*
  validation, and GPU-AV instruments the shader — it adds a bounds check to every buffer access in
  the traversal loop, which is the entire inner loop of a ray tracer. Measured both ways on the same
  scene, same GPU, same resolution:

  | 1280x720, GTX 1650 Ti | Debug + validation | Release |
  |-----------------------|--------------------|---------|
  | Frame | 16.9 ms | **3.7 ms** |
  | Trace | 16.6 ms | **3.4 ms** |
  | Post-processing | 0.33 ms | **0.29 ms** |
  | Hierarchy build (CPU) | 120 ms | **14 ms** |

  Note which row barely moves. Post-processing is a fixed number of texture reads per texel with no
  data-dependent addressing, so there is little for GPU-AV to instrument; the trace pass is nothing
  but data-dependent addressing. That is also why the ratio cannot be applied as a blanket correction
  to old figures — it is not one factor, it is a different factor per pass.

  `docs/ACOUSTICS.md` has been corrected: its delay table and the "twenty-six frames" headline were
  computed from the 14.4 ms figure and are now computed from 3.7 ms. The journal entries below are
  left as they were written — they record what was measured at the time, and rewriting them would
  hide the mistake rather than fix it.

- **The hierarchy build does not need parallelising yet, and the plan to do so was based on the wrong
  number.** 14 ms in Release, not the 120 ms that a Debug measurement suggested — about 2 % of a
  650 ms startup, nearly all of which is Vulkan device and pipeline creation that threading cannot
  touch. Parallelising it now would buy roughly 10 ms of startup for something like eighty lines of
  concurrent code, against a repo rule that says "don't over-engineer". It becomes worth doing in
  Phase 6, when creatures move and the hierarchy is rebuilt every tick — a 14 ms rebuild against a
  frame budget is a completely different proposition from a 14 ms cost paid once.

### 2026-08-01

- The floor has relief. Value noise, three octaves, quantised to six levels, up to five metres over
  128 m — ported from the original TronGrid's `src/terrain.cpp`, which had already solved this and
  had already found the good part: quantising the height turns smooth swells into terraces, which
  reads as something built rather than grown. Three octaves rather than the original's four, because
  a fourth octave's features would be a quarter of the wavelength across, barely wider than one quad
  at this cell size, and detail finer than the mesh can carry does not appear — it aliases, which on
  a mirror reads as noise.
- One thing was deliberately **not** ported. The original computes its normals from the raw
  un-quantised heightmap, so that reflections stretch across the terrace steps like a dent in a car
  bonnet. That is right for a rasteriser shading with interpolated normals and wrong here: this is a
  ray tracer with perfect mirrors, where the reflection direction *is* the geometric normal, so a
  normal that disagreed with the geometry would leak light and self-intersect. Lite keeps true
  per-face normals, and the shattered facets that result are the honest answer rather than the
  pretty one.
- The relief costs nothing. It displaces vertices that already existed, so the triangle count and
  the hierarchy over it are unchanged.
- Two things broke on contact with a non-flat floor and had to be fixed. Objects were planted at
  y = 0 and started floating or sinking; they now sample the ground, and specifically the *lowest*
  of their four footprint corners, because a terrace step can run straight through a six-metre
  footprint and something set into the ground reads as deliberate where something hovering above it
  reads as broken. The cinematic camera dipped to 4.4 m, which is below the highest terrace, so its
  mean height went from 7 m to 10 m.
- The floor mesh and the neon tubes agree exactly at shared grid vertices because both call the same
  surface function with coordinates computed by the same expression, not because heights are copied
  from one to the other.
- The relief is deterministic by construction: an integer hash rather than a seeded generator, so
  the same coordinates give the same bits on every machine and every run. A recording that renders a
  different landscape each time it is made is not a recording.

### 2026-07-31

- Added a recording mode and `tools/record_flyby.py`, which flies a closed camera path and encodes
  the result as the looping animation now at the top of the README.
- The camera path is periodic by construction: every oscillation completes a whole number of cycles
  over the loop, and the last frame stops one step short of the first rather than repeating it, so
  the clip has no seam.
- Recording deliberately does not use the swapchain, frames in flight or real time. Each frame is
  submitted alone and waited on, which makes the output identical on every run — a recording that
  flickers differently each time it is made is not a recording. The readback it needs is the same
  operation a creature sensor will need in Phase 6.
- Frames are written as binary PPM, which needs no library at all. The encoder in `tools/` reads
  them and ffmpeg does the rest.
- One real defect surfaced from actually running it: the post-processing stage ended by declaring
  `eBlit` as the destination stage of its final barrier, because the only consumer at the time was
  the swapchain blit. A copy is a different pipeline stage, so recording read the image unordered
  against that transition and synchronisation validation reported a read-after-write hazard. The
  stage has no business assuming how its output is consumed, and now says `eAllTransfer`.
- GIF encoding is two-pass against a single global palette computed across the whole clip. A
  per-frame palette makes a mostly black image shimmer, which is the one artefact nobody misses.
  Frame count dominates the file size, then width, then palette size; dithering costs size rather
  than saving it, because it adds noise the compression cannot pack.
- The first cut moved too much. It has been retuned to be slower, calmer and stuttery: the wobble
  amplitudes are down by roughly two thirds, the height and radius modulation are gentler, and the
  clip now runs 84 frames at 12 rather than 100 at 20. The three changes are not independent, which
  is the pleasant part. A low frame rate lengthens the clip while *reducing* the frame count, since
  duration is frames divided by rate; and GIF stores each frame as only the pixels that changed
  since the last, so a calm camera over a mostly black world leaves most of the image untouched
  between frames. Slower, calmer and smaller all at once: 5.1 MiB over five seconds became 4.2 MiB
  over seven.
- `images/` sits at the repository root, and `tools/` carries its own README, a requirements file
  that is honestly empty, and a `.venv` placeholder so the convention is visible without reading a
  document to discover it.
- A security review of the recording code turned up four small defects, none of them a
  vulnerability and all of them the local user's own input coming back at them:
    - `writePpm` left the stream to close itself, so the destructor flushed the tail of the buffer
      *after* the error check had already passed. A disk filling in the last few kilobytes of the
      last frame wrote a truncated PPM and reported success. It now closes explicitly and checks.
    - `std::stoul` is specified in terms of `strtoul`, which negates a leading minus into the
      unsigned result rather than failing, so `--frames -1` quietly became 4294967295 frames.
      `std::from_chars` parsing straight into the `uint32_t` rejects the sign, trailing junk and
      out-of-range values in one call.
    - `--width 0` handed Vulkan a zero-extent image, a valid-usage violation with no validation
      layers in a release build to catch it. Now bounded against the device's own
      `maxImageDimension2D`, the same guard the swapchain already applies to a minimised window.
    - ffmpeg expands `%` sequences across the whole input path rather than just the filename, so a
      checkout under a directory containing one broke the encode. The frames directory is now passed
      as the working directory instead of being baked into the pattern. Re-recording afterwards
      produced a byte-identical GIF, which is the check that the change altered nothing else.
- CodeQL raised one high-severity alert, `cpp/path-injection`, for `--output` reaching
  `std::ofstream`. Dismissed as a false positive. The dataflow it reports is real; the security
  conclusion is not. The query treats `argv` as attacker-controlled, which is correct for setuid
  binaries, CGI programs and services, and wrong for a desktop renderer the user launches
  themselves: the only party who can set `--output` is the person who already owns the process and
  could do the same with a shell redirect. The write is not even a general primitive, since the
  filename is always `frame_%05d.ppm` from a loop counter and the contents are a render of a fixed
  scene. It fires only because code scanning runs with the `remote_and_local` threat model, whose
  whole purpose is to treat the command line as tainted.
- Two tempting responses to that alert were both rejected. Validating the path would restrict the
  only legitimate user — `--output D:/renders/tonight` is perfectly reasonable — and probably
  would not clear the alert anyway, because the query recognises only particular
  normalise-plus-contain barriers rather than hand-rolled checks. A `query-filters` exclusion would
  have been worse: there is no CodeQL workflow in this repository at all, scanning runs under
  GitHub's default setup, which never reads that file, so the change would have been silently
  inert. Making it work would mean owning a hand-maintained workflow forever and disarming the
  query repo-wide.
- **Revisit this in Phase 6.** Once the creature roster resolves Program library paths out of a
  config file (`docs/PROGRAM_INTERFACE.md`), the path stops coming from the command line and starts
  coming from a file that a downloaded creature pack could write. At that point the query is right
  and confinement becomes a real requirement rather than theatre.
- Merging the first Python file taught code scanning a new language, and it promptly raised four
  more path-injection alerts against `tools/record_flyby.py` (plus one command-line-injection).
  All dismissed for the reason above: the flagged values are `--executable`, `--gif` and `--mp4`,
  typed by the operator into a script that runs as the operator. Expect a repeat whenever a new
  language first lands on main — default setup adds analysers automatically.
- The CI cache cleanup had never deleted a single CodeQL cache. Its grouping used the whole key as
  the group name for any family it did not recognise, every CodeQL overlay-base key is unique
  (commit SHA, run ID and attempt are baked in), and a group of one has nothing older than its
  newest member to delete. Fifteen caches, about 260 MB, one more per push. The fix names the
  family and groups it by its stable prefix — cache version, config hash and language, deliberately
  excluding the CLI version so a CodeQL upgrade does not leave a permanent orphan; the corpus held
  nine dead 2.26.1 entries proving the point. Unknown families are now reported as a workflow
  warning instead of being silently kept, since silence is exactly how this went unnoticed. The
  second copy of the cleanup logic inlined in `ci_main.yml` — which had already drifted (no
  pagination past 100 caches, no dry run) — is deleted in favour of the shared script both
  workflows now `require()`. Verified by running the real module against the repository's real
  cache list with the API mocked: 16 CodeQL caches collapse to one family, keep-newest holds the
  current HEAD's base, both Vulkan SDK caches and the npm cache survive untouched.

### 2026-07-30 (evening)

- Etape 6 (Phase 4 milestone): **post processing.** The tracer now writes linear radiance into an
  rgba16f target and stops there. Everything that turns radiance into a picture — the bloom
  pyramid, the fitted ACES curve, sRGB encoding, the vignette — belongs to a new stage in
  `src/postprocess.*`. Exposure moved there too, since how much radiance survives into a
  displayable range is a property of the tone mapping rather than of the tracer.
- Both post-processing shaders had been compiled and SPIR-V-validated on every build since the
  original infrastructure port, and never once dispatched. Three reviewers read their contracts in
  parallel before any host code was written, which caught two things that would each have cost an
  afternoon: the tone mapping shader declared its output format `unknown`, which requires the
  optional `shaderStorageImageWriteWithoutFormat` device feature — declaring `rgba8`, which is
  what the host actually binds, removes the dependency entirely, and a renderer aimed at modest
  hardware should not demand optional features. And the shader reads the bloom image
  *unconditionally*, scaling it by the strength afterwards, so a strength of zero does not excuse
  leaving that image undefined: an uninitialised half float multiplied by zero is zero only if it
  was not a NaN. Mip 0 is cleared when bloom is off.
- The bloom upsample was stepping visibly. It computed its source coordinate with
  `thread_id.xy / 2`, so all four texels of a destination block sampled the same source position —
  a tent filter followed by nearest-neighbour magnification. It now samples bilinearly, which is
  the difference between a glow and a staircase, for 0.08 ms.
- The three bloom entry points were being compiled into three separate modules containing a third
  of the same code each. They share a descriptor set layout and a pipeline layout, so they are now
  one module and three pipelines.
- Measured at 1280x720 on the reference GTX 1650 Ti: 14.4 ms in total, of which the entire
  post-processing chain — six mip levels down and back up, tone mapping, sRGB — is **0.31 ms**.
  The ray tracing is still essentially the whole cost.

### 2026-07-30 (afternoon)

- Etape 5 (Phase 3 milestone): **the full ray tree.** A surface now splits the ray rather than only
  reflecting it. Recursion is unavailable in a compute shader, so the tree is walked with an
  explicit stack: the reflected branch is followed immediately and the refracted one is set aside.
  Snell refraction, total internal reflection, and a throughput cutoff all arrive with it. Still
  entirely deterministic — no sampling anywhere, so still no denoiser.
- Measured the two cost axes properly, and the first attempt was wrong in an instructive way. A
  sweep run from the Bash shell reported that tree depth was free, which was an artefact: that
  shell has no MSVC environment, so every C++ compile failed while the shader kept rebuilding, and
  the build output had been redirected to /dev/null. Only the shader-side parameter was really
  changing. Repeated with the environment set, both axes cost roughly a doubling across their
  range and the two multiply. The table now in trace.slang is the corrected data.
- Settled on a stack of three and a depth of six: 13.7 ms at 1280x720 on the reference GTX 1650 Ti,
  inside a sixty-frame budget. Worth keeping in proportion — the User's window is the most
  expensive consumer this renderer will ever have, and a creature sensor is a fourteenth of it at
  most.

**Pre-merge review of Phase 2, run before this work landed.** Five reviewers across separate
dimensions, every finding then attacked by a skeptic instructed to refute it: sixteen raised, eight
verified, three survived. All three were real.

- A descriptor pool created without `eFreeDescriptorSet` while `vk::raii::DescriptorSets` frees its
  sets on destruction — a validation error on every clean shutdown. Found independently by running
  the thing; fixed.
- The acquire path treated `eSuboptimalKHR` as a reason to abandon the frame. But vulkan-hpp throws
  for out-of-date and returns suboptimal as success, so that branch fired only when the image *had*
  been acquired and its semaphore *would* be signalled — leaving it signalled with nothing to
  consume it, and handing it to the next acquire in violation of
  VUID-vkAcquireNextImageKHR-semaphore-01779. Dragging a window edge produces this constantly. A
  suboptimal image is a usable image, so it is now rendered and presented and the swapchain rebuilt
  afterwards.
- `0 * inf = NaN` in the slab test. A ray straight down has two zero direction components, so their
  reciprocals are infinite; launch it from a coordinate lying exactly on a node boundary — which
  every integer coordinate does on an axis-aligned grid — and the multiplication is NaN, which both
  `std::min` and `std::max` propagate, turning a box the ray passes through into a miss. The
  reviewer reproduced it numerically: 49 of 641 swept positions, every one an exact integer. The
  new regression test was checked by reverting the fix and confirming it fails.
- Two honesty fixes the review turned up without them being defects: the surface-area-heuristic's
  leaf guard had its traversal term on the wrong side of the comparison and could never fire, and
  the depth-cap test's comment described an input it does not actually build.

### 2026-07-30

- Etape 4 (Phase 2 milestone): **the Grid is ray traced.** The rasteriser is gone — no graphics
  pipeline, no depth buffer, no vertex buffer, and `triangle.slang` deleted with them. Every pixel
  is now a ray walked through a hand-built hierarchy in a compute shader.
- `libs/bvh` holds the builder: a binned surface-area-heuristic split over twelve buckets, with a
  depth cap that exists because the shader traverses with a fixed-size stack. The cap and the
  stack are the same constant, and a test builds pathological geometry to prove the tree never
  exceeds it. Eleven tests in total; the important one traces six thousand random rays and
  requires the accelerated traversal to agree with a brute-force sweep on every single one.
- Two bugs worth recording. The traversal originally used a negative sentinel for a missed
  bounding box, which sorted a missed child *in front of* a hit one and silently discarded the
  hit; it is positive infinity now, in both the host and the shader. And a sanity check on the
  brute-force comparison — that at least a twentieth of the rays actually strike something —
  failed on the first run, revealing that the test cloud was so sparse the comparison was
  agreeing on four thousand mutual misses and proving nothing.
- GPU-assisted validation earned its keep: it reported an out-of-bounds read on the material
  buffer, which turned out to be the shader still declaring the acoustic fields that the ABI
  de-bloating had removed from the host struct. The sizes are now asserted on the C++ side.
- Measured on the reference GTX 1650 Ti at 1280x720: **4.0 ms per frame, around 250 fps**, of
  which the trace is 3.97 ms and the blit 0.04 ms. The claim that this renderer runs comfortably
  on modest hardware is no longer a claim.
- Neon tubes are now thin (25 mm) and sit a centimetre off the floor. Phase 1 needed them wide and
  lifted to survive a depth buffer and sub-pixel rasterisation; the tracer has neither problem.
- Added standing pillars to the scene, because a flat world has nothing to reflect: a tube lying a
  centimetre above a mirror casts a reflection a centimetre below it, which merges with the tube.

### 2026-07-19

- Genesis: repo initialised from TronGrid's genesis commit shape (19-file scaffold,
  hello world, CI green on first push). Org `ai-quokka-wannabe` created the same
  evening; genesis commit pushed.
- Etape 1: inherited all portable infrastructure and the five internal libraries
  from TronGrid; sources moved to `src/`; identity adapted throughout.
- Etape 1b: inherited the Vulkan foundation as well — instance, device, surface, swapchain,
  VMA allocator, camera, components and scene — each adapted to the lite scope. The device
  requests nothing beyond Vulkan 1.3 core (dynamic rendering, synchronisation2) plus the
  swapchain extension: no ray tracing, no mesh shaders, no bindless. Ported the three shaders
  that survive the scope cut (triangle, postprocess, bloom) and dropped the rest. Documentation
  written fresh for Lite: VISION, ARCHITECTURE, PROGRAM_INTERFACE, DEV_ENV_SETUP.
- Etape 2b: wrote `docs/PERCEPTION.md` from a verified literature review — sensor presets and
  their published anchors, the human comparison (the sharpest patch of human vision is a 240×240
  image; 4K exists as insurance against unpredictable gaze, which a simulated sensor never
  needs), what embodied-AI research actually feeds to networks, the acoustic budget, and twelve
  design rules. Scope sharpened across the docs at the same time: this repo is the stage, not
  the actor — it renders senses and applies actions, Programs are DLL/SO plugins behind a
  plain C ABI, and no cognition, learning or behaviour model belongs here.
- Etape 3 (Phase 1 milestone): **flying through the neon grid.** Ported the last of what
  TronGrid could lend — procedural geometry, the GPU timestamp profiler and the User camera
  controller — plus `docs/MATERIALS.md`. Wired them into a real frame loop with a depth buffer.
  Two rendering bugs found and fixed by looking at the output rather than the code: neon tubes
  z-fought the floor at distance (lifted 5 cm, near plane moved to 0.5 m), then still broke into
  dashes because a 2 cm strip is sub-pixel at 90 m (widened to 12 cm — the compute tracer will
  filter properly and can afford thinner tubes later).
- Stripped the compatibility machinery out of the ABI and the speculative fields out of the code,
  on the principle that a pre-1.0 project with no users owes nobody compatibility. Gone:
  `struct_size` everywhere, duplicated version members, hand-written padding, reserved fields for
  modalities that do not exist, the unused acoustic fields in `Material` (which halves it to two
  std430 rows), and the inherited terrain generators whose terraced look belongs to the parent
  project rather than this flat mirror floor.
- Fixed two real synchronisation defects that had been present since Phase 1 and that only
  surfaced once validation was read carefully: the swapchain layout transition was not ordered
  against the acquire semaphore's wait stage, and both frames in flight shared one depth image.
  Each frame now owns a depth buffer. Validation runs clean, and the profiler reports **0.37 ms
  per frame — 2.2% of a 60 fps budget** for 24,832 triangles on the reference GTX 1650 Ti.
- Reviewed the full list of senses the Grid forwards to a Program, and fixed what the review found.
  The eye fields could not express the sensor presets `PERCEPTION.md` already specifies — several
  eyes, non-RGB channel counts, or a sample-direction list rather than a raster — so `TglEyeDesc`
  and `TglEyeView` replace them and the ABI version went to 1 while breaking changes are still
  free. Added vestibular sensing and thermoreception, both nearly free and both biologically
  grounded; recorded that hearing has no sound sources yet, that echolocation falls out of hearing
  plus a vocalisation action, and that chemoreception is deliberately absent but arguably the most
  faithful sense for the smallest preset. A compass was considered and rejected: it would hand a
  Program the structure the Grid exists to make it earn.
- Material model unified: the `MaterialKind` enum is gone. Every surface is one perfectly smooth
  material that reflects, transmits and emits at once, so "mirror", "neon" and "glass" are named
  points in a continuous space rather than types. No shader branch, and a glowing translucent
  surface — a neon tube in a glass envelope — is now expressible, which the enum could not do.
- Etape 2 (Phase 0 milestone): **triangle on screen.** Window, instance, device, swapchain,
  dynamic rendering, Slang compilation with SPIR-V validation, and double-buffered frame
  synchronisation all verified end to end on the reference GTX 1650 Ti. Validation layers report
  no errors; they confirm the device requirements are honest by disabling their own ray-query,
  trace-ray and mesh-shading checks because the hardware lacks those features. Five test suites
  pass.
