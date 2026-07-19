# TronGrid Lite

A deliberately simple Vulkan renderer for the Tron aesthetic â€” clean geometry, emissive
materials, perfectly reflective surfaces, neon glow against infinite black.

This is the little sibling of [TronGrid](https://github.com/MatejGomboc/tron_grid),
stripped of the hardcore graphics programme (mesh shaders, hardware ray tracing,
bindless everything). Same world, same soul, a fraction of the machinery â€” so it runs
on modest GPUs and stays small enough to read in an afternoon. It is the reference
world for the AI creature brains developed in this organisation: creatures perceive
through rendered frames, never through scene graph access.

## Status

Early development. Currently proving the toolchain (Phase 0).

## Platforms

| Platform | Windowing | Status |
|----------|-----------|--------|
| Windows  | Win32     | Active |
| Linux    | X11       | Active |

## Requirements

- **Vulkan SDK** 1.3+ ([LunarG](https://vulkan.lunarg.com/))
- **C++20** compiler (MSVC, GCC, or Clang)
- **CMake** 3.16+
- **Ninja** build system

### Target Hardware

Any Vulkan 1.3 GPU. **No ray tracing extensions required** â€” ray tracing is done in
plain compute shaders. Development machine is a GTX 1650 Ti laptop; if it runs well
there, it runs well anywhere.

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

1. **Simple over clever** â€” the whole renderer should fit in one head
2. **Deterministic ray tracing** â€” perfect mirrors and emissive lighting need no
   Monte Carlo, no denoiser, no RT hardware: a fixed, shallow Whitted ray tree
3. **Creature-first resolution** â€” animal eyes resolve far less than 800Ă—600;
   creature vision renders tiny (sensor-sized), only the human spectator window
   renders big
4. **One world, two senses** â€” a single BVH answers both visual rays and acoustic
   rays; surfaces carry optical and acoustic properties together
5. **Portable brains** â€” the AI player interface stays compatible with big TronGrid,
   so a brain developed here runs there unchanged
6. **Incremental** â€” every phase produces something visible

## Material Model

The entire surface vocabulary of the world:

| Material | Properties |
|----------|------------|
| Mirror   | Single colour (mostly black), perfectly specular |
| Emissive | Mirror + light emission (the neon) |
| Glass    | Simple transparency with refraction |

That is all. No roughness, no microfacets, no texture pipeline. The aesthetic *is*
the algorithm: Whitted ray tracing, 1980 edition, running in compute.

## Build Plan

| Phase | Goal | Milestone |
|-------|------|-----------|
| 0 - Foundation | Prove the toolchain | Triangle on screen |
| 1 - Infrastructure | Window, swapchain, frame loop | Fly through a wireframe grid |
| 2 - Compute Tracer | BVH + primary rays in compute | Mirror world, first bounce |
| 3 - Full Ray Tree | Reflections, emissives, glass | The Tron look, correct |
| 4 - Post Processing | Bloom, tonemapping | The Tron look, beautiful |
| 5 - Acoustic Rays | Audio paths through the same BVH | Echoes and occlusion |
| 6 - AI Players | Creature sensor interface | A brain plugs in and perceives |

## References

- [Vulkan Tutorial](https://vulkan-tutorial.com)
- [vkguide.dev](https://vkguide.dev)
- [Sascha Willems Vulkan Samples](https://github.com/SaschaWillems/Vulkan)
- [Slang Shader Language](https://shader-slang.org)
- Whitted, T. (1980). *An improved illumination model for shaded display*

## The Vision

> A digital creature will wake up in this world.
> It will see neon lines against infinite black â€”
> through eyes that resolve less than an old CRT,
> and that is enough, because it has always been enough
> for every animal that ever chased, hid, and played.
>
> The grid does not need four thousand lines of resolution.
> It needs to be *true*: every reflection honest,
> every echo travelling the same world as every glint.
> A small world, rendered simply, perceived completely.

## Licence

Copyright Â© 2026 Matej Gomboc <https://github.com/ai-quokka-wannabe/tron-grid-lite>.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

See the attached [LICENCE](LICENCE) file for more info.
