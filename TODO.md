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

- [x] Transmission: split the ray at a surface rather than reflecting only
- [x] Snell refraction with total internal reflection
- [x] Raise the bounce limit and add a throughput cutoff
- [x] Glass in the test scene

## Etape 6 — Phase 4: post processing

- [x] Render to an HDR target instead of tone mapping inside the tracer
- [x] Wire up the bloom chain that is already compiled
- [x] Move tone mapping to postprocess.slang and its fitted ACES curve

## Etape 7 — Phase 5: acoustic rays

- [ ] Give surfaces something to be heard: sound sources in the world
- [ ] Acoustic ray traversal through the same hierarchy
- [ ] Energy histogram per listener, banded by octave
- [ ] Fill `hearing_samples` in the agent interface

## Journal

### 2026-07-30 (evening)

- Etape 6 (Phase 4 milestone): **post processing.** The tracer now writes linear radiance into an
  rgba16f target and stops there. Everything that turns radiance into a picture — the bloom
  pyramid, the fitted ACES curve, sRGB encoding, the vignette — belongs to a new stage in
  `src/postprocess.*`. Exposure moved there too, since how much radiance survives into a
  displayable range is a property of the tone mapping rather than of the tracer.
- Both post-processing shaders had been compiled and SPIR-V-validated on every build since the
  original infrastructure port, and never once dispatched. Three agents read their contracts in
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
  inside a sixty-frame budget. Worth keeping in proportion — the spectator window is the most
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
