# TronGrid Lite

A deliberately simple Vulkan renderer for the Tron aesthetic — clean geometry, emissive
materials, perfectly reflective surfaces, neon glow against infinite black.

**This world is built for AI agents, not for people.** There is no player, no controls and no
gameplay. The inhabitants are AI creatures whose brains live in separate plugin repositories in
this organisation; they perceive the world only through their own small rendered sensor images,
never through scene graph access. Humans watch through a spectator window — a free-flight debug
camera for development and observation.

It is a fresh, independent project. It reuses infrastructure and foundation code from the
author's earlier [TronGrid](https://github.com/MatejGomboc/tron_grid) renderer (same author,
same licence) but leaves behind the hardcore graphics programme — mesh shaders, hardware ray
tracing, bindless everything — so that it runs on modest GPUs and stays small enough to read in
an afternoon.

## Status

Early development. Currently proving the toolchain (Phase 0).

## Platforms

| Platform | Windowing | Status |
|----------|-----------|--------|
| Windows  | Win32     | Active |
| Linux    | XCB       | Active |

## Requirements

- **Vulkan SDK** 1.3+ ([LunarG](https://vulkan.lunarg.com/))
- **C++20** compiler (MSVC, GCC, or Clang)
- **CMake** 3.25+
- **Ninja** build system

### Target Hardware

Any Vulkan 1.3 GPU. **No ray tracing extensions required** — ray tracing is done in plain
compute shaders. The reference development machine is a GTX 1650 Ti laptop, which exposes no
Vulkan ray tracing extensions at all; if it runs well there, it runs well anywhere.

## Building

### Windows (MSVC)

```bash
cmake --preset windows-msvc
cmake --build build/windows-msvc --config Debug
```

### Windows (Clang-CL)

```bash
cmake --preset windows-clang-cl
cmake --build build/windows-clang-cl --config Debug
```

### Linux (GCC)

```bash
sudo apt-get install libxcb1-dev
cmake --preset linux-x11-gcc
cmake --build build/linux-x11-gcc --config Debug
```

### Linux (Clang)

```bash
sudo apt-get install libxcb1-dev
cmake --preset linux-x11-clang
cmake --build build/linux-x11-clang --config Debug
```

## Design Principles

1. **Agents, not players** — every design decision serves the creatures and their brains; the
   human is an observer, never a participant
2. **Simple over clever** — the whole renderer should fit in one head
3. **Deterministic ray tracing** — perfect mirrors and emissive lighting need no Monte Carlo, no
   denoiser and no ray tracing hardware: a fixed, shallow Whitted ray tree
4. **Creature-first resolution** — animal eyes resolve far less than 800×600; creature vision
   renders tiny, and only the spectator window renders big (see [docs/PERCEPTION.md](docs/PERCEPTION.md))
5. **One world, two senses** — a single BVH answers both visual rays and acoustic rays; surfaces
   carry optical and acoustic properties together
6. **Incremental** — every phase produces something visible

## Material Model

The entire surface vocabulary of the world:

| Material | Properties |
|----------|------------|
| Mirror   | Single colour (mostly black), perfectly specular |
| Emissive | Mirror plus light emission (the neon) |
| Glass    | Simple transparency with refraction |

That is all. No roughness, no microfacets, no texture pipeline. The aesthetic *is* the
algorithm: Whitted ray tracing, 1980 edition, running in compute.

## Build Plan

| Phase | Goal | Milestone |
|-------|------|-----------|
| 0 - Foundation | Prove the toolchain | Triangle on screen |
| 1 - Infrastructure | Window, swapchain, frame loop | Fly through a wireframe grid |
| 2 - Compute Tracer | BVH plus primary rays in compute | Mirror world, first bounce |
| 3 - Full Ray Tree | Reflections, emissives, glass | The Tron look, correct |
| 4 - Post Processing | Bloom, tonemapping | The Tron look, beautiful |
| 5 - Acoustic Rays | Audio paths through the same BVH | Echoes and occlusion |
| 6 - AI Agents | Creature sensor interface | A brain plugs in and perceives |

## Documentation

| Document | Purpose |
|----------|---------|
| [docs/VISION.md](docs/VISION.md) | What this world is for and where it is going |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the renderer is put together |
| [docs/PERCEPTION.md](docs/PERCEPTION.md) | Creature sensor resolutions and the biology behind them |
| [docs/AGENT_INTERFACE.md](docs/AGENT_INTERFACE.md) | The plugin contract between world and brain |
| [docs/DEV_ENV_SETUP.md](docs/DEV_ENV_SETUP.md) | Setting up a development environment |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to contribute |
| [STYLE.md](STYLE.md) | Code style conventions |
| [TODO.md](TODO.md) | Roadmap, active tasks and development journal |

## References

- [Vulkan Tutorial](https://vulkan-tutorial.com)
- [vkguide.dev](https://vkguide.dev)
- [Sascha Willems Vulkan Samples](https://github.com/SaschaWillems/Vulkan)
- [Slang Shader Language](https://shader-slang.org)
- Whitted, T. (1980). *An improved illumination model for shaded display*

## The Vision

> A digital creature will wake up in this world.
> It will see neon lines against infinite black —
> through eyes that resolve less than an old CRT,
> and that is enough, because it has always been enough
> for every animal that ever chased, hid and played.
>
> The grid does not need four thousand lines of resolution.
> It needs to be *true*: every reflection honest,
> every echo travelling the same world as every glint.
> A small world, rendered simply, perceived completely.

## Licence

Copyright © 2026 Matej Gomboc <https://github.com/ai-quokka-wannabe/tron-grid-lite>.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

See the attached [LICENCE](LICENCE) file for more info.
