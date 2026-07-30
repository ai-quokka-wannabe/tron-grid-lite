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
- Slang shaders: `triangle.slang` (Phase 0 smoke test), `postprocess.slang` (ACES tonemap and
  sRGB encode) and `bloom_downsample.slang` (three bloom entry points), all compiled and
  SPIR-V-validated at build time.
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

### Changed

- Unified the material model: `MaterialKind` is gone and `Material` gained a continuous
  `transmission` parameter. Every surface reflects, transmits and emits simultaneously, so there
  is no shader branch and a glowing translucent surface is expressible.
