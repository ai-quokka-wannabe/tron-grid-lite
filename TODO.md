# TODO

The live source of truth for TronGrid Lite work. New work is added as etapes;
criteria are ticked when satisfied; the Journal records what actually happened.

## Roadmap (phases)

| Phase | Goal                          | Milestone                          | Status |
|-------|-------------------------------|------------------------------------|--------|
| 0     | Prove the toolchain           | Triangle on screen                 | **Done** |
| 1     | Window, swapchain, frame loop | Fly through a wireframe grid       | **Done** |
| 2     | BVH + primary rays in compute | Mirror world, first bounce         | **Done** |
| 3     | Full ray tree                 | Reflections, emissives, glass      | Pending |
| 4     | Post processing               | Bloom, tonemapping                 | Pending |
| 5     | Acoustic rays                 | Echoes and occlusion via same BVH  | Pending |
| 6     | AI players                    | Creature sensor interface plugs in | Pending |

## Etape 1 — Adopt project infrastructure from TronGrid

- [x] Port linting configs, governance docs, templates, workflows, Claude commands
- [x] Port internal libraries (testing, signals, logging, math, window) with tests
- [x] Move sources to `src/`, add GPL v3 licence headers, include volk.cpp in build
- [x] Adapt all name references to TronGrid Lite / ai-quokka-wannabe
- [x] CI green on all matrix jobs after adoption

## Etape 2 — Phase 0: triangle on screen

- [x] Vulkan instance + debug messenger (vk::raii)
- [x] Physical device selection (prefer discrete GPU)
- [x] Logical device + queue creation
- [x] Window via WindowLib (Win32 / XCB)
- [x] Swapchain (MAILBOX present mode), dynamic rendering
- [x] Graphics pipeline: triangle.slang (vertex + fragment)
- [x] Frame synchronisation (fences + semaphores)
- [x] Triangle on screen

## Etape 3 — Phase 1: window, swapchain and frame loop

- [x] Spectator camera wired to input (free flight, for the human observer only)
- [x] Neon grid geometry to fly through
- [x] Depth buffer, recreated with the swapchain
- [x] Frame timing and GPU timestamp profiling with a once-per-second summary

## Etape 4 — Phase 2: the compute ray tracer

- [x] Triangle and material storage buffers
- [x] BVH builder on the host, uploaded as a storage buffer
- [x] Compute traversal kernel: primary rays only, mirror surfaces
- [x] Write results into an offscreen image, blitted into the swapchain
- [x] Retire the rasteriser: no graphics pipeline, no depth buffer, no vertex buffer

## Etape 5 — Phase 3: the full ray tree

- [ ] Transmission: split the ray at a surface rather than reflecting only
- [ ] Snell refraction with total internal reflection
- [ ] Raise the bounce limit and add a throughput cutoff
- [ ] Glass in the test scene

## Journal

### 2026-07-30

- Etape 4 (Phase 2 milestone): **the world is ray traced.** The rasteriser is gone — no graphics
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
  written fresh for Lite: VISION, ARCHITECTURE, AGENT_INTERFACE, DEV_ENV_SETUP.
- Etape 2b: wrote `docs/PERCEPTION.md` from a verified literature review — sensor presets and
  their published anchors, the human comparison (the sharpest patch of human vision is a 240×240
  image; 4K exists as insurance against unpredictable gaze, which a simulated sensor never
  needs), what embodied-AI research actually feeds to networks, the acoustic budget, and twelve
  design rules. Scope sharpened across the docs at the same time: this repo is the stage, not
  the actor — it renders senses and applies motor intent, agents are DLL/SO plugins behind a
  plain C ABI, and no cognition, learning or behaviour model belongs here.
- Etape 3 (Phase 1 milestone): **flying through the neon grid.** Ported the last of what
  TronGrid could lend — procedural geometry, the GPU timestamp profiler and the spectator
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
- Reviewed the full list of senses the world forwards to a brain, and fixed what the review found.
  The eye fields could not express the sensor presets `PERCEPTION.md` already specifies — several
  eyes, non-RGB channel counts, or a sample-direction list rather than a raster — so `TglEyeDesc`
  and `TglEyeView` replace them and the ABI version went to 1 while breaking changes are still
  free. Added vestibular sensing and thermoreception, both nearly free and both biologically
  grounded; recorded that hearing has no sound sources yet, that echolocation falls out of hearing
  plus a vocalisation action, and that chemoreception is deliberately absent but arguably the most
  faithful sense for the smallest preset. A compass was considered and rejected: it would hand a
  brain the structure the world exists to make it earn.
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
