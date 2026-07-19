# Vision

## The Idea

TronGrid Lite is a small, deliberately simple Vulkan renderer that builds a world **for AI agents only**.

There is no human player. There is no gameplay, no score, no win condition, no story. The inhabitants of this world are
**AI creature agents** — software creatures that perceive the world through rendered images and rendered sound, and act
back into it through a narrow motor interface. A human being never inhabits this world; a human being only ever
**spectates** it, through a debug window driven by a free-flight camera used for development and observation.

That distinction shapes every decision in this repository. When a design question comes up, the answer is not "what
feels good to play" — it is "what does a creature need to perceive, and what does a developer need to see while
debugging it".

## An Independent Project

TronGrid Lite is an **independent, fresh project** in the `ai-quokka-wannabe` organisation. It is not a component,
module, or sub-project of anything else, and it has no parent codebase that owns it.

What it does do is **reuse infrastructure and foundation code** from the author's earlier TronGrid renderer: the
internal libraries (testing, signals, logging, maths, windowing), the build presets, the code style, and a good deal of
hard-won Vulkan bring-up knowledge. That reuse is a head start, not a dependency. TronGrid Lite has its own goals, its
own scope, its own roadmap, and its own much smaller renderer.

The two projects also aim at opposite ends of the hardware spectrum. The earlier renderer targets a top-end GPU with
hardware ray tracing. TronGrid Lite targets a **GTX 1650 Ti laptop**, which exposes exactly zero Vulkan ray-tracing
extensions. Everything here must run there.

## The Inhabitants: AI Creature Agents

The creatures that live in the Grid are not written in this repository. Each brain is a **separate plugin repository**
in the same organisation, loaded as a shared library at runtime. TronGrid Lite provides the world and the senses; the
brain repositories provide the minds.

The brains climb a **development ladder from the simplest animals upward**:

- Start at the bottom — reflex arcs, taxis and kineses, the behavioural repertoire of something with a few hundred
  neurons. Move toward light. Move away from a wall.
- Then simple associative learning: this glow preceded that outcome, so approach it or avoid it.
- Then spatial behaviour: place memory, path following, homing.
- Then anything further the ladder supports.

Nothing about this ladder is baked into the renderer. The renderer's only obligation is to be an **honest world**: what
a creature senses must be a consequence of where it is and what is around it, never a privileged read of scene state.
No entity list. No ground-truth positions. No cheating. A creature knows what its sensors resolve, and nothing more.

The human observer sits outside that contract entirely. The spectator window is a debugging instrument — it shows the
same world from a free camera at a comfortable resolution, so a developer can watch what a creature is doing and judge
whether the world is behaving. It is never an input to any creature.

## The Aesthetic

The world is Tron-inspired cyberspace: **neon lines against infinite black**.

- **Infinite black** — there is no sky, no skybox, no horizon, no fog, no terrain. A ray that hits nothing returns
  black. The void is genuinely empty, and that emptiness is free to render.
- **Everything is a perfect mirror** — every surface in the world is perfectly specular. Most of them are almost black,
  so they read as dark polished planes that catch and repeat the neon.
- **Emissive geometry is the only light source** — there are no point lights, no directional lights, no ambient term.
  Light exists because some geometry glows. The neon tubes and glowing edges that form the visual language of the world
  *are* the lighting rig.
- **Glass** — a small amount of simple transparent geometry with Snell refraction, for depth and for something for a
  creature's optics to be confused by.

The whole surface vocabulary of the world is three materials — mirror, emissive, glass — and that is a feature. With
perfect mirrors and emissive geometry there is nothing stochastic to integrate: the ray tree is **deterministic and
shallow**, Whitted 1980 style. No Monte Carlo sampling, therefore no noise, therefore no denoiser, therefore no
temporal accumulation and no ghosting. The image a creature sees on frame *N* depends only on the world state on
frame *N*.

### Colour Palette

| Colour | Hex | Role |
|--------|-----|------|
| Cyan | `#00FFFF` | Primary neon |
| Magenta | `#FF00FF` | Accent neon |
| Orange | `#FF8800` | Energy / warning neon |
| White | `#FFFFFF` | Highlights |
| Black | `#000000` | The void, and most surfaces |

## Creature Senses

### Vision

A creature's eye is a **render target of its own**, and it is tiny. Sensor resolutions in the range of 64 x 64 to
256 x 256 are the normal case, not a degraded mode. This is biologically honest — the eyes of most animals resolve far
less than a computer display — and it is also what makes hand-written compute ray tracing affordable on a modest GPU.
A 64 x 64 sensor is 4096 primary rays. A creature can have several eyes, pointing in several directions, and still cost
less than one spectator frame.

The spectator window is separate and larger. It renders the same world with the same tracer, at a resolution a human
can actually look at.

### Hearing

Later in the roadmap, the same acceleration structure that answers visual rays will answer **acoustic rays**. Sound in
this world travels along the same geometry that light does: it reflects off the same mirrors, is occluded by the same
walls, and passes through the same glass. To make that possible, every surface carries **both optical and acoustic
properties** from the start — a reflectivity for photons and an absorption coefficient for pressure waves, stored side
by side in the same material record.

One world, two senses, one traversal.

## What TronGrid Lite Deliberately Is Not

The single most important design rule in this project is *stay small*. The renderer should be readable in an afternoon
and should run on a laptop GPU from 2020. That rules out a long list of otherwise attractive technology:

- **No hardware ray tracing** — no `VK_KHR_acceleration_structure`, no `VK_KHR_ray_query`, no
  `VK_KHR_ray_tracing_pipeline`. The reference GPU exposes none of them. Rays are traced by hand in ordinary compute
  shaders, against a BVH this project builds itself into storage buffers.
- **No mesh shaders and no meshlets** — the geometry budget of this world is small enough that the classic pipeline is
  never the bottleneck, and the tracer does not rasterise anyway.
- **No bindless descriptor indexing requirement** — a handful of storage buffers and a handful of images, bound
  explicitly, is easier to follow and works everywhere.
- **No GPU-driven pipeline and no heavy allocator layer** — no indirect draw machinery to debug.
- **No ReSTIR, no SVGF denoiser, no temporal accumulation** — deterministic Whitted shading produces no noise to
  denoise.
- **No volumetric fog, no procedural terrain, no skybox** — the world is infinite black by design.
- **No textures, no roughness, no microfacets, no PBR material system** — three materials, each described by a few
  numbers.

Every item on that list is a deliberate omission, not a missing feature. Each one removed a subsystem that would have
needed writing, debugging, and explaining, and none of them are needed for neon lines against infinite black.

## Design Principles

1. **Simple over clever.** The whole renderer should fit in one head.
2. **Agents first, humans never.** The world exists to be perceived by creatures. Human-facing output is a debugging
   convenience and is allowed to be crude.
3. **Deterministic rendering.** Same world state in, same image out. Essential for training, testing, and reproducing
   creature behaviour.
4. **Honest embodiment.** A creature receives sensor data only. No scene graph access, no entity list, no ground truth.
5. **Creature-sized resolution.** Sensors render tiny. Only the spectator window renders large.
6. **One world, two senses.** A single BVH serves visual and acoustic rays; surfaces carry optical and acoustic
   properties together.
7. **Own everything that matters.** The BVH, the tracer, the maths, the windowing, the logging and the test harness are
   all in-house. External dependencies are limited to the Vulkan SDK, Volk, vulkan-hpp and Slang.
8. **Platform parity.** Windows (Win32) and Linux (XCB) are both first-class. No macOS, no Wayland, no mobile.
9. **Incremental.** Every phase produces something visible.

## Phase Roadmap

Each phase produces a working, demonstrable result. The canonical task checklist lives in `TODO.md`.

| Phase | Goal | Milestone |
|-------|------|-----------|
| 0 | Foundation | Toolchain proven; triangle on screen |
| 1 | Window, swapchain, frame loop | Fly a debug camera through a wireframe grid |
| 2 | BVH + primary rays in compute | Mirror world, first bounce, traced by hand |
| 3 | Full deterministic ray tree | Reflections, emissive light, glass with Snell refraction |
| 4 | Post-processing | Bloom and tonemapping — the neon finally glows |
| 5 | Acoustic rays | Echo and occlusion traced through the same BVH |
| 6 | AI agent sensor interface | A creature brain plugs in and perceives |

```text
Phase 0 --> 1 --> 2 --> 3 --> 4 --+--> 5 --+--> 6
```

Phases 0 to 4 build the world. Phase 5 gives it a second sense. Phase 6 opens it to its inhabitants — and from that
point on, the interesting work happens in the brain repositories, not here.

## The Point

> A digital creature will wake up in this world.
> It will see neon lines against infinite black —
> through eyes that resolve less than an old CRT,
> and that is enough, because it has always been enough
> for every animal that ever chased, hid, and played.
>
> The Grid does not need four thousand lines of resolution.
> It needs to be *true*: every reflection honest,
> every echo travelling the same world as every glint.
> A small world, rendered simply, perceived completely.
