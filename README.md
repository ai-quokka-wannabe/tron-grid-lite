# TronGrid Lite

A deliberately simple Vulkan renderer for the Tron aesthetic — clean geometry, emissive
materials, perfectly reflective surfaces, neon glow against infinite black.

![A camera banking through a neon grid of mirrored floor, glowing pillars and refracting glass](images/flyby.gif)

Every pixel of that is a ray, traced in an ordinary compute shader against a bounding volume
hierarchy this project builds itself. **The GPU it was recorded on exposes no ray-tracing
extensions at all** — it is a laptop GTX 1650 Ti, and it renders that scene in about fourteen
milliseconds a frame. Record your own with [`tools/record_flyby.py`](tools/record_flyby.py).

**The Grid is built for Programs, not for people.** There is no player, no controls and no
gameplay. The User watches through a debug window — a free-flight camera for development and
observation.

**It is the stage, not the actor.** TronGrid Lite renders senses and applies actions; it does
not think. Programs load as shared-library plugins (DLL on Windows, SO on Linux), receive small
buffers of senses, and return actions — so the Grid stays **completely agnostic about how any
Program works inside**. No Program internals live here, and none should: Programs are other
repositories' business, behind a plain C ABI.

It is a fresh, independent project. It reuses infrastructure and foundation code from the
author's earlier [TronGrid](https://github.com/MatejGomboc/tron-grid) renderer (same author,
same licence) but leaves behind the hardcore graphics programme — mesh shaders, hardware ray
tracing, bindless everything — so that it runs on modest GPUs and stays small enough to read in
an afternoon.

## A Note on the Vocabulary

This project uses Tron's language throughout, and unapologetically. Watching *Tron* (1982) is
strongly recommended and arguably a prerequisite. If you have not, the table below will get you
through the API — but you are missing out.

| Word | What it means here |
|------|--------------------|
| the Grid | The world this renderer simulates |
| Program | The thing that thinks — a plugin, one per creature |
| creature | The body the Grid simulates, which a Program drives |
| User | The human at the debug window, watching |
| tick | One simulation step |
| senses | What a creature perceives during a tick |
| actions | What a Program asks its creature's body to do |
| rez / derez | A Program arriving on the Grid, and leaving it |

## Status

Early development, and the picture above is real output rather than a target — though it is now a
little out of date, having been recorded before the floor gained its terraced relief.

Phases 0 to 5 are done. The Grid is traced in a compute shader against a hierarchy built on the
host; surfaces reflect and refract; a bloom chain and an ACES tone curve turn radiance into a
picture; and the same hierarchy now answers acoustic rays, delivering an impulse response — energy
against delay — at an ear. One Grid, two senses, one traversal.

What remains is Phase 6: the interface a Program plugs into, so that something can finally perceive
any of it.

## Choosing a GPU

`--list-gpus` reports every Vulkan device on the machine, whether it can run the renderer, and why
not if it cannot. `--gpu <index>` forces one, overriding the score that otherwise always prefers a
discrete device.

```text
Vulkan devices on this machine: 2.
  [0] AMD Radeon(TM) Graphics — integrated, Vulkan 1.3.260 — USABLE, score 1100. Run with --gpu 0.
  [1] NVIDIA GeForce GTX 1650 Ti — discrete, Vulkan 1.4.341 — USABLE, score 10100. Run with --gpu 1.
```

Both of those are exercised on every change worth the name. **Pixels are bit-identical on one
device and close but not identical across two** — cross-device identity is not reachable, because
IEEE-754 pins neither fused-multiply-add contraction nor the transcendentals. The difference is
measured rather than assumed; see [docs/PROGRAM_INTERFACE.md](docs/PROGRAM_INTERFACE.md)
§ Determinism and Replay.

## Checking it

Three modes run headless and answer a question rather than drawing a picture. Each needs a GPU, so
none of them runs in CI.

| Command | Question |
|---------|----------|
| `--verify-acoustics` | Do the acoustic shader and the host gather agree? |
| `--verify-scene` | Do they still agree with the Grid placed at an angle, where transform arithmetic can actually be wrong? |
| `--benchmark` | What does each GPU pass cost? |

```text
Trace 3.342041 ms | post 0.294431 ms | frame 3.636273 ms.
That is 275.006836 frames per second at 1280x720, GPU only.
```

`--benchmark` reports the device's own timestamps after discarding ten warm-up frames, and walks the
same fixed camera path `--record` does so that two runs compare. It writes nothing.

## Platforms

| Platform | Windowing | Status |
|----------|-----------|--------|
| Windows  | Win32     | Active |
| Linux    | XCB       | Active |

## Requirements

- **Vulkan SDK** 1.4.335.0+ ([LunarG](https://vulkan.lunarg.com/)) — the version CI builds against.
  The real constraint is that `slangc` and `spirv-val` must be present in the SDK's `bin` directory,
  because CMake requires both at configure time
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

1. **Programs, not players** — every design decision serves the creatures and their Programs; the
   User is an observer, never a participant
2. **Simple over clever** — the whole renderer should fit in one head
3. **Deterministic ray tracing** — perfect mirrors and emissive lighting need no Monte Carlo, no
   denoiser and no ray tracing hardware: a fixed, shallow Whitted ray tree
4. **Creature-first resolution** — animal eyes resolve far less than 800×600; creature vision
   renders tiny, and only the User's window renders big (see [docs/PERCEPTION.md](docs/PERCEPTION.md))
5. **One Grid, two senses** — a single BVH answers both visual rays and acoustic rays. `Material`
   carries optical properties only; acoustically every surface is a perfect mirror, and the one
   authored acoustic number — how loudly a surface sings — lives in its own table indexed by the
   same material slot
6. **Incremental** — every phase produces something visible

## Material Model

The entire surface vocabulary of the Grid:

Every surface is one perfectly smooth material that is reflective, translucent and emissive at
once — no types, no branches, just parameters:

| Parameter | Meaning |
|-----------|---------|
| `colour` | Tint applied to reflected and transmitted light. Usually near-black |
| `index_of_refraction` | Drives Fresnel everywhere, and Snell refraction where light passes through |
| `emission` | Radiance the surface emits — this is the neon |
| `transmission` | How much non-reflected light passes through rather than being absorbed |

"Mirror", "neon" and "glass" are named points in that space rather than categories, so a glowing
translucent surface is as ordinary as any of them.

There is no roughness, no microfacet model and no textures. That is not a simplification of
physically-based rendering but its smooth limit: as roughness goes to zero the microfacet
distribution collapses and the BRDF reduces analytically to Fresnel-weighted mirror reflection
plus refraction. Each bounce is therefore exactly one ray, the tree is shallow and deterministic,
and there is no Monte Carlo variance to denoise. The aesthetic *is* the algorithm: Whitted ray
tracing, 1980 edition, running in compute.

## Build Plan

| Phase | Goal | Milestone |
|-------|------|-----------|
| 0 - Foundation | Prove the toolchain | Triangle on screen |
| 1 - Infrastructure | Window, swapchain, frame loop | Fly through a wireframe grid |
| 2 - Compute Tracer | BVH plus primary rays in compute | Mirror world, first bounce |
| 3 - Full Ray Tree | Reflections, emissives, glass | The Tron look, correct |
| 4 - Post Processing | Bloom, tonemapping | The Tron look, beautiful |
| 5 - Acoustic Rays | Audio paths through the same BVH | Echoes and occlusion |
| 6 - Programs | Creature sensor interface | A Program plugs in and perceives |

## Documentation

| Document | Purpose |
|----------|---------|
| [docs/VISION.md](docs/VISION.md) | What the Grid is for and where it is going |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the renderer is put together |
| [docs/MATERIALS.md](docs/MATERIALS.md) | The material model, the Fresnel and refraction maths, and the HDR path |
| [docs/PERCEPTION.md](docs/PERCEPTION.md) | Creature sensor resolutions and the biology behind them |
| [docs/ACOUSTICS.md](docs/ACOUSTICS.md) | How hearing works on the Grid, and the decisions behind it |
| [docs/PROGRAM_INTERFACE.md](docs/PROGRAM_INTERFACE.md) | The plugin contract between the Grid and a Program |
| [docs/RELATED_WORK.md](docs/RELATED_WORK.md) | What research labs build in this area, and what is genuinely unusual here |
| [docs/research/](docs/research/) | Literature surveys and citations that design decisions rest on |
| [docs/DEV_ENV_SETUP.md](docs/DEV_ENV_SETUP.md) | Setting up a development environment |
| [tools/README.md](tools/README.md) | Scripts that operate on the renderer, including the flyby recorder |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to contribute |
| [STYLE.md](STYLE.md) | Code style conventions |
| [TODO.md](TODO.md) | Roadmap and open etapes |

## References

- [Vulkan Tutorial](https://vulkan-tutorial.com)
- [vkguide.dev](https://vkguide.dev)
- [Sascha Willems Vulkan Samples](https://github.com/SaschaWillems/Vulkan)
- [Slang Shader Language](https://shader-slang.org)
- Whitted, T. (1980). *An improved illumination model for shaded display*

## The Vision

> A digital creature will wake up on the Grid.
> It will see neon lines against infinite black —
> through eyes that resolve less than an old CRT,
> and that is enough, because it has always been enough
> for every animal that ever chased, hid, and played.
>
> The Grid does not need four thousand lines of resolution.
> It needs to be *true*: every reflection honest,
> every echo travelling the same Grid as every glint.
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
