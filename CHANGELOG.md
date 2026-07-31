# Changelog

All notable changes to TronGrid Lite are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Initial project scaffold: build system, CI, editor config, and hello world.
- Full project infrastructure inherited from [TronGrid](https://github.com/MatejGomboc/tron_grid):
  linting configs (clang-tidy, markdownlint), governance documents (contributing guide,
  code of conduct, security policy, style guide), issue and PR templates, CI workflows
  (main, PR validation, release, cache cleanup), and Claude assistant commands.
- Internal static libraries inherited from TronGrid: `testing`, `signals`, `logging`,
  `math`, and `window` (Win32 / XCB), each with their own test suites.
- `src/` layout with GPL v3 licence headers; volk translation unit included in the build.
- Vulkan foundation inherited from TronGrid and adapted to the lite scope: instance with
  validation and debug messenger, physical/logical device selection, platform surface,
  swapchain with resize handling, VMA allocator wrapper, spectator camera, scene components.
- Material model expressed in code: mirror, emissive and glass, with fields reserved for the
  acoustic properties the shared BVH will need.
- Slang shaders: `postprocess.slang` (ACES tonemap and sRGB encode) and `bloom_downsample.slang`
  (three bloom entry points), compiled and SPIR-V-validated at build time.
- Documentation: `docs/VISION.md`, `docs/ARCHITECTURE.md`, `docs/AGENT_INTERFACE.md` and
  `docs/DEV_ENV_SETUP.md`.
- `docs/PERCEPTION.md` — the sensor presets the world renders (`elegans`, `insect-min`,
  `insect-mid`, `insect-high`, `rodent`, `macropod`), the published measurements that set their
  sizes, and twelve binding design rules. Every figure is cited and every biology-to-pixels
  translation is flagged as a translation.
- **Phase 0 milestone reached: a triangle renders in the spectator window** via dynamic
  rendering, verified on a GTX 1650 Ti with validation layers enabled and no errors reported.
- Procedural geometry (`src/geometry.hpp`): flat mirror grid floor, terraced value-noise terrain,
  neon tubes along grid lines with an accent colour on major lines, and a box primitive. Plain
  `std::vector` output with no Vulkan dependency, flat-shaded per-face vertices ready for a BVH.
- GPU timestamp profiler (`src/profiler.hpp`) with per-pass exponential moving averages and a
  once-per-second summary. Self-disables on hardware without timestamp support.
- Spectator controller (`src/spectator.hpp`) — free-flight camera for the human observer, the
  only interactive element in the project.
- `docs/MATERIALS.md` — Fresnel, Snell refraction, the HDR path and tonemapping, kept to what the
  smooth-surface model actually uses.
- **Phase 1 milestone reached: flying through the neon grid** — 24,832 triangles with a depth
  buffer that is recreated alongside the swapchain.

- **Phase 2 milestone reached: the world is ray traced.** `libs/bvh` builds a binned
  surface-area-heuristic hierarchy on the host; `src/trace.slang` walks it in an ordinary compute
  shader with a fixed-size stack and shades every surface with Schlick Fresnel. Measured at
  **4.0 ms per frame at 1280x720 on a GTX 1650 Ti**, a GPU with no ray-tracing extensions at all.
- `libs/bvh` — the hierarchy, with eleven tests. The load-bearing one traces six thousand random
  rays and requires the accelerated traversal to agree with a brute-force sweep exactly.
- `src/tracer.hpp` — uploads the world to device-local storage buffers through a staging copy, owns
  one output image per frame in flight, and records the dispatch.

- **Phase 3 milestone reached: the full ray tree.** Transmissive surfaces split the ray instead of
  only reflecting it, with Snell refraction, total internal reflection and a throughput cutoff.
  The tree is walked with an explicit stack because compute shaders have no recursion, and the
  whole thing remains deterministic. Glass slabs and a glowing translucent column now stand in the
  scene. 13.7 ms at 1280x720 on a GTX 1650 Ti.

### Changed

- Unified the material model: `MaterialKind` is gone and `Material` gained a continuous
  `transmission` parameter. Every surface reflects, transmits and emits simultaneously, so there
  is no shader branch and a glowing translucent surface is expressible.
- Reworked the sensory half of the agent ABI, and bumped `TGL_BRAIN_ABI_VERSION` to 1. A body now
  carries an array of `TglEyeDesc`, so a creature may have several eyes, none at all, a channel
  count other than three, and a sample-direction list instead of a raster — all of which the
  sensor presets required and the previous single-RGB-raster field could not express.
- Added two modalities to `TglSenses`: **vestibular** sensing (`specific_force`,
  `angular_velocity`) and **thermoreception** (`irradiance`). Both are nearly free — the numbers
  already exist in the motion integrator and the ray tracer respectively — and the first matters
  particularly in a world of perfect mirrors, where a reflected floor is indistinguishable from
  real space to vision alone.
- Documented that hearing still lacks a prerequisite: nothing in the world currently emits sound.

- `docs/RELATED_WORK.md` — the prior art, with credit to the projects this one stands on:
  OpenWorm and c302 for the worm end of the ladder, CompoundRay and I2Bot for compound-eye
  rendering, NeuroMechFly v2 and the virtual rodent for embodied bodies and brains, and the
  Animal-AI Environment for cognitive testing. Also the short list of what is genuinely unusual
  here, and a plain statement of what this project is not attempting.

### Removed

- Compatibility machinery from the agent ABI: per-struct `struct_size` fields, duplicated
  `abi_version` members, hand-written padding members, and fields reserved for modalities that do
  not exist yet. A single `TGL_BRAIN_ABI_VERSION` check at load time is now the whole mechanism.
  Those devices exist to let mismatched builds keep working, and the interface has no users to
  keep working — they were clutter in every struct.
- The acoustic fields from `Material`. Nothing reads them, and reserving two std430 rows for them
  doubled the material buffer; they arrive when hearing does. `Material` is now exactly two
  16-byte rows with no padding at all, down from four.
- The procedural terrain generators inherited from TronGrid, along with their value-noise
  helpers. They were unused, and terraced heightmaps are the parent project's look rather than
  this one's flat mirror floor. 124 lines of `src/geometry.*` went with them.

- **Phase 4 milestone reached: post processing.** The tracer writes linear radiance into an rgba16f
  target; a new stage runs the bloom pyramid over it, applies the fitted ACES RRT+ODT curve,
  encodes sRGB and optionally vignettes. `src/postprocess.hpp` owns the mip chain, the pipelines
  and the descriptor sets. The whole chain costs 0.31 ms at 1280x720 on a GTX 1650 Ti.
- Exposure moved from the tracer to the tone mapping pass, where it belongs.

- A recording mode (`--record`) that flies a scripted camera loop and writes one image per frame,
  plus `tools/record_flyby.py`, which drives it and encodes the result as a seamless looping GIF.
  The animation at the top of the README is its output.
- `tools/` gained a README, a requirements file and a `.venv` placeholder.

### Fixed

- The CI cache cleanup never deleted CodeQL overlay-base caches: any key outside the two families
  it knew about became a group of one, and the keep-newest-delete-rest loop cannot fire on a group
  of one. Fifteen dead caches (~260 MB) had accumulated, one per push to main. The cleanup script
  now knows the CodeQL family, reports any unknown family loudly instead of silently keeping it
  forever, and the drifted inline copy of the logic in `ci_main.yml` is gone — both the every-push
  step and the manual workflow now run the same module.
- The swapchain acquire path abandoned the frame on `VK_SUBOPTIMAL_KHR`, leaving the acquire
  semaphore signalled with nothing to consume it and reusing it on the next acquire, which
  VUID-vkAcquireNextImageKHR-semaphore-01779 forbids. Resizing the window triggered it routinely.
- `0 * inf = NaN` in the bounding box slab test made axis-parallel rays launched from exact grid
  coordinates miss boxes they pass through. Both the host reference and the shader are fixed, and
  a regression test covers it.
- The descriptor pool is created with `eFreeDescriptorSet`, which `vk::raii::DescriptorSets`
  requires when it frees its sets at shutdown.
- The surface-area heuristic's leaf guard had its traversal term on the wrong side of the
  comparison and could never fire.
- The bloom upsample derived its source coordinate by integer division, so every texel of a
  destination block sampled the same source position and the glow magnified as visible steps. It
  now samples bilinearly.
- `postprocess.slang` declared its output image format as `unknown`, which requires the optional
  `shaderStorageImageWriteWithoutFormat` device feature. It declares `rgba8` instead, which is what
  the host binds.
- The post-processing stage declared `eBlit` as the destination stage of its final barrier, so a
  consumer that copies rather than blits — which the recording mode does — was unordered against
  the layout transition. It declares `eAllTransfer` now.
- Bloom mip 0 is cleared when bloom is disabled. The tone mapping shader reads it unconditionally,
  and an undefined half float multiplied by zero is only zero if it was not a NaN.

- Two genuine synchronisation defects that Vulkan's synchronisation validation caught, both
  present since Phase 1. The swapchain layout transition claimed `eTopOfPipe` as its source stage
  while the acquire semaphore is waited on at `eColorAttachmentOutput`, so nothing ordered the
  transition after the acquire. And a single depth image was shared by both frames in flight,
  which is a real race rather than a validation nicety — each frame in flight now owns one.
