# Vision

## The Idea

TronGrid Lite is a small, deliberately simple Vulkan renderer that builds the Grid — a world **for Programs only**.

There is no human player. There is no gameplay, no score, no win condition, no story. The inhabitants of the Grid are
**Programs**, each driving a creature that perceives the Grid through rendered images and rendered sound, and acts
back into it through a narrow motor interface. A User never inhabits the Grid; a User only ever **watches** it,
through a debug window driven by a free-flight camera used for development and observation.

That distinction shapes every decision in this repository. When a design question comes up, the answer is not "what
feels good to play" — it is "what does a creature need to perceive, and what does the User need to see while
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

## The Inhabitants: Programs

The Programs that live on the Grid are not written in this repository, and their workings are none of this
repository's business. Each Program is a **separate plugin** — a DLL on Windows, an SO on Linux — loaded at runtime
behind a plain C ABI. **TronGrid Lite is the stage, not the actor.** It renders senses and applies actions. What
happens between the two is entirely the plugin's affair, and the Grid is deliberately **agnostic about how any Program
works inside**: a lookup table, a neural network, a hand-written reflex arc and a large model are all the same thing
from here.

That agnosticism is a design constraint, not modesty. The moment the Grid knows something about how a Program thinks,
it starts serving that assumption, and it stops being a world. So there is no cognition, no learning and no behaviour
model anywhere in this codebase, and there should never be one.

What the renderer does owe the Programs is **honesty**: what a creature senses must be a consequence of where it is and
what is around it, never a privileged read of scene state. No entity list. No ground-truth positions. No cheating. A
creature knows what its sensors resolve, and nothing more — see [PERCEPTION.md](PERCEPTION.md) for how small that
deliberately is.

The User sits outside that contract entirely. The debug window is an instrument for development — it shows the
same Grid from a free camera at a comfortable resolution, so the User can watch what a creature is doing and judge
whether the Grid is behaving. It is never an input to any creature.

## The Aesthetic

The Grid is Tron-inspired cyberspace: **neon lines against infinite black**.

- **Infinite black** — there is no sky, no skybox, no horizon, no fog. A ray that hits nothing returns
  black. The void is genuinely empty, and that emptiness is free to render.
- **Everything is a perfect mirror** — every surface on the Grid is perfectly specular. Their tints are close to white,
  and it is Fresnel rather than the tint that keeps them dim head-on, so they read as dark polished planes that catch and
  repeat the neon.
- **Emissive geometry is the only light source** — there are no point lights, no directional lights, no ambient term.
  Light exists because some geometry glows. The neon tubes and glowing edges that form the visual language of the Grid
  *are* the lighting rig.
- **Glass** — a small amount of simple transparent geometry with Snell refraction, for depth and for something for a
  creature's optics to be confused by.

The whole surface vocabulary of the Grid is three materials — mirror, emissive, glass — and that is a feature. With
perfect mirrors and emissive geometry there is nothing stochastic to integrate: the ray tree is **deterministic and
shallow**, Whitted 1980 style. No Monte Carlo sampling, therefore no noise, therefore no denoiser, therefore no
temporal accumulation and no ghosting. The image a creature sees on tick *N* depends only on the Grid state on
tick *N*.

### Colour Palette

| Colour | Linear RGB | Role |
|--------|------------|------|
| Cyan | (0.02, 0.62, 1.0) | Primary neon — ordinary grid lines |
| Orange | (1.0, 0.36, 0.03) | Accent neon — major grid lines |
| White | (1.0, 1.0, 1.0) | Mirror tints and highlights |
| Black | (0.0, 0.0, 0.0) | The void |

Two neon hues and no more: a third starts to look like a colour test chart rather than a world.
[MATERIALS.md § The Neon Palette](MATERIALS.md#the-neon-palette) is canonical for these values and
for the emissive magnitudes that go with them.

## Creature Senses

### Vision

A creature's eye is a **sensor of its own**, and it is tiny. Sensors run from a few samples to a few tens of thousands,
as a raster or as an explicit sample-direction list — see
[PERCEPTION.md § Sensor presets](PERCEPTION.md#sensor-presets). This is biologically honest — the eyes of most animals
resolve far less than a computer display — and it is also what makes hand-written compute ray tracing affordable on a
modest GPU. A 64 x 64 raster is 4096 primary rays; the smallest preset is two. A creature can have several eyes,
pointing in several directions, or none at all, and still cost less than one debug-window frame.

The debug window is separate and larger. It renders the same Grid with the same tracer, at a resolution the User
can actually look at.

### Hearing

Later in the roadmap, the same acceleration structure that answers visual rays answers **acoustic rays**. Sound on the
Grid travels along the same geometry that light does — the same hierarchy, the same slab test, the same triangle
intersection — but it does not behave the same way once it arrives, and the differences are the interesting part.

Light is instantaneous for a renderer; sound is not, so an acoustic ray carries its accumulated path length and the
answer is a function of time rather than a value. Every surface is a **perfect acoustic mirror**: no absorption, no
scattering, and no transmission, so where light passes through a glass slab, sound bounces off it and behind it there
is quiet. **The one surface the optical renderer treats as transparent is the one the acoustic renderer treats as most
opaque**, which is precisely why a creature with both senses learns something neither sense could tell it alone.

So the two senses share the geometry and share nothing else. `Material` carries optical properties only; what a
surface does acoustically is a single authored number — how loudly it sings — in its own small table beside it, and
the Grid's neon is the only thing that sings. See [ACOUSTICS.md](ACOUSTICS.md).

One Grid, two senses, one traversal.

## What TronGrid Lite Deliberately Is Not

The single most important design rule in this project is *stay small*. The renderer should be readable in an afternoon
and should run on a laptop GPU from 2020. That rules out a long list of otherwise attractive technology:

- **No hardware ray tracing** — no `VK_KHR_acceleration_structure`, no `VK_KHR_ray_query`, no
  `VK_KHR_ray_tracing_pipeline`. The reference GPU exposes none of them. Rays are traced by hand in ordinary compute
  shaders, against a BVH this project builds itself into storage buffers.
- **No mesh shaders and no meshlets** — the geometry budget of the Grid is small enough that the classic pipeline is
  never the bottleneck, and the tracer does not rasterise anyway.
- **No bindless descriptor indexing requirement** — a handful of storage buffers and a handful of images, bound
  explicitly, is easier to follow and works everywhere.
- **No GPU-driven pipeline and no heavy allocator layer** — no indirect draw machinery to debug.
- **No ReSTIR, no SVGF denoiser, no temporal accumulation** — deterministic Whitted shading produces no noise to
  denoise.
- **No volumetric fog, no skybox** — the Grid is infinite black by design. The floor's terraced relief is the one
  departure from flatness, and it earns its place acoustically rather than scenically: a flat mirror sends every
  reflection away and returns none of it, whereas terrace risers standing square to the ground throw sound back across
  the Grid, which is where the Grid's echoes come from.
- **No textures, no roughness, no microfacets, no PBR material system** — three materials, each described by a few
  numbers.

Every item on that list is a deliberate omission, not a missing feature. Each one removed a subsystem that would have
needed writing, debugging, and explaining, and none of them are needed for neon lines against infinite black.

## Design Principles

1. **Simple over clever.** The whole renderer should fit in one head.
2. **Programs first, Users never.** The Grid exists to be perceived by creatures. User-facing output is a debugging
   convenience and is allowed to be crude.
3. **Deterministic rendering.** Same Grid state in, same image out. Essential for training, testing, and reproducing
   creature behaviour.
4. **Honest embodiment.** A creature receives senses only. No scene graph access, no entity list, no ground truth.
5. **Creature-sized resolution.** Sensors render tiny. Only the debug window renders large.
6. **One Grid, two senses.** A single BVH serves visual and acoustic rays; surfaces will carry optical and acoustic
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
| 6 | Program sensor interface | A Program plugs in and perceives |

```text
Phase 0 --> 1 --> 2 --> 3 --> 4 --+--> 5 --+--> 6
```

Phases 0 to 4 build the Grid. Phase 5 gives it a second sense. Phase 6 opens it to its inhabitants — and from that
point on, the interesting work happens in the Program repositories, not here.

## The Point

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
