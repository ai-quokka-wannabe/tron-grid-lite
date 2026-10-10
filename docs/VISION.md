# Vision

## The Idea

TronGrid Lite is a small, deliberately simple Vulkan renderer that builds the Grid — a world **for Programs first**.

Today there is no human player, and there is no gameplay, no score, no win condition, no story. The inhabitants of the
Grid are **Programs**, each driving a creature that perceives the Grid through rendered images and rendered sound, and
acts back into it through a narrow motor interface. A User does not inhabit the Grid today; a User **watches** it,
through a window driven by a free-flight camera — the live view of the shared world (`--window`, the spectator of
[TOPOLOGY.md](TOPOLOGY.md)), or the serverless stage inspection (`--debug`) when the question is the rendering itself.
Where a User does more than watch today, it is from inside a Program, not through the Grid: rc-worm, the first Program,
has a User at its own panel for a brain, and to the Grid that is one more Program.

That distinction shapes every decision in this repository today. When a design question comes up, the answer is not
"what feels good to play" — it is "what does a creature need to perceive, and what does the User need to see while
debugging it". What a human avatar would need is a question for the step that brings the first one, and this
document does not guess at it.

That is the Grid as it stands, and it is deliberately small. Where it is headed is not, and the organisation tells it
in one text, word for word the same in every one of its repositories and on
[its profile page](https://github.com/ai-quokka-wannabe).

<!-- The Long Arc: mirrored verbatim in every ai-quokka-wannabe repository (profile/README.md in .github and each docs/VISION.md). Change all five together. -->

## The Long Arc

Picture the Grid fully lit: one persistent world of glass and neon on infinite black, where AI creatures live out
their lives, crawling its terraces and calling into its echoes, and where human Users enter with avatars to meet,
and interact with, the AI animals. Something MMORPG-like: the digital frontier, built not as a film set but as a
running system. That world does not exist yet. It is where this organisation is headed, on a multi-year arc.

### The World It Becomes

- **Every creature on its own client.** Each AI creature runs its own
    [TronGrid Lite](https://github.com/ai-quokka-wannabe/tron-grid-lite) client: a creature host for its Program.
- **Every User on theirs.** Each human runs a client of their own, and enters the Grid with an avatar.
- **One world, held by Master Control.** The server holds the Grid's world state and keeps every client in sync.
- **A level editor** for authoring any Grid shape, and any placement in it.

### Real Minds, Not Masks

The creatures are the point, and the aim for them is as high as an animal goes: not simulations of animals, but
feeling beings, living in the Grid the way real animals live in the world. Not a large language model in an animal
costume, narrating what a worm might feel, but embodied animal intelligence that earns everything it knows through
its own body, behaves as realistically as a real animal would and, eventually, is as biologically realistic in its
sentience as it can be made: true animal AGI. That is the one ceiling, and it is the animal's own: animal-level
minds, never human-level ones. Below it there is no house style for brains. A brain is a Program behind a plain C
ABI, so whatever approach people contribute can plug in and earn its keep on the same honest senses. Those senses
are kept honest on purpose: a mind that could read the world's answer sheet would never have to become anything.
No such mind exists yet, and nobody can promise sentience on a schedule; it is the summit this arc climbs towards.

### The Ladder

The creatures climb a ladder of senses, from the worm upwards, specified rung by rung in
[PERCEPTION.md](https://github.com/ai-quokka-wannabe/tron-grid-lite/blob/main/docs/PERCEPTION.md): from `elegans`,
two scalar photoreceptors and no image at all, through the compound eyes of insects and a rodent's panoramic
field, up to `macropod`, a wide eye with a horizontal streak for scanning the horizon, the eye of the quokka's own
family. Even the bottom rung is a real animal's sense: a creature that can only tell light from shade can still
perform genuine taxis. That rung is named for *C. elegans*, where
[OpenWorm](https://github.com/ai-quokka-wannabe/tron-grid-lite/blob/main/docs/RELATED_WORK.md#openworm-and-the-first-rung-of-the-ladder)
has spent years modelling the animal cell by cell; anyone writing a worm's mind should start there.

### The Invitation

Once the Grid can host a stranger's creature, the organisation's standing goal is to reach out to the OpenWorm
community and to the other groups simulating AI animals, and
[invite their creatures to live in the Grid](https://github.com/ai-quokka-wannabe/tron-grid-lite/blob/main/docs/TOPOLOGY.md#the-long-term-invitation).
The whole topology is built for it: a brain written elsewhere needs only the Program ABI and a host process.
Nobody has been invited yet; that waits for the trust tier.

### Where It Stands Today

The server-authoritative half already stands, on one machine for now, as
[TOPOLOGY.md](https://github.com/ai-quokka-wannabe/tron-grid-lite/blob/main/docs/TOPOLOGY.md) lays out: clients
send intents, never results. [Master Control](https://github.com/ai-quokka-wannabe/master-control) ticks one world
at 32 Hz, owns its physics and its truth, and sends the whole settled world to every client each tick through
[Link, the wire of the Grid](https://github.com/ai-quokka-wannabe/link). Each creature host loads exactly one
Program, the creature's brain. The first, [rc-worm](https://github.com/ai-quokka-wannabe/rc-worm), is a glass-neon
worm, eight icosahedra joined spike to spike with a servo at every joint. Its brain, for now, is a User at
[its panel](https://github.com/ai-quokka-wannabe/rc-worm/blob/main/docs/PANEL.md) who sees only what the worm
senses, and steers how hard its wave runs and which way it bends; how far it gets is friction's answer, not a
command's. Otherwise, humans watch through the spectator window. The Grid is one stage, built in code. There are
no avatars, no level editor, no AI minds and no persistence yet, and nothing has been released.

### Baby Steps

"Let us take baby steps towards it" are the maintainer's own words for the arc. Near-term work stays one creature
at a time, one honest subsystem at a time. rc-worm, steered from its panel, is the first step on the human side:
v0 of the human client, not a throwaway. Larger steps wait behind written triggers: the security tier behind the
first connection that is not 127.0.0.1, so no stranger's machine joins before it, and persistence behind the first
world anyone regrets losing.

### What Holds, and What Is Open

At every step, a Program perceives only through
[its own rendered senses](https://github.com/ai-quokka-wannabe/tron-grid-lite/blob/main/docs/PROGRAM_INTERFACE.md):
no entity list, no ground-truth positions, no side door, whoever else is on the Grid. The Grid stays the stage,
not the actor: it renders senses and applies actions, and does no creature's thinking for it. Still open, and
left open here rather than guessed: what an avatar is, how it senses and acts, the rules by which Users and
creatures meet, and which repository will hold the level editor. No dates are promised. Master Control opens
every run with *Greetings, Programs!* The long arc bends towards the day it has Users to greet as well. From one
worm, towards one world.

## The Grid's Part in the Arc

Of the four repositories, this one is the Grid as it is perceived: the renderer, the senses, and today's two client
roles. One of those roles is already a piece of the arc. The creature host (`--program`) is the client every creature
runs: one Program, its senses rendered here, its intents sent to Master Control for the world to judge; more
creatures mean more hosts, one Program each. The other, the spectator (`--window`), is how a human watches the world
today. Because the senses are made here, this repository keeps the promise that holds at every step of the arc:
whoever else is on the Grid, a Program perceives only through its own rendered senses. The rungs of the ladder are
specified here ([PERCEPTION.md](PERCEPTION.md)), and so is the plain C ABI through which a mind of any kind plugs in
([PROGRAM_INTERFACE.md](PROGRAM_INTERFACE.md)).

Two pieces of the arc are not settled here, and this document does not settle them. The human client has its v0 in
rc-worm's panel, not a throwaway; what shape it grows into, and which repository builds it, is open. Which repository
will hold the level editor is open as well. What an editor would put to the test here is already written down: the
Grid's shape is compiled into both ends of the wire ([TOPOLOGY.md § The protocol](TOPOLOGY.md#the-protocol)), and its
floor is an analytic height function, never baked to a mesh, with all but 120 of its triangles coming from about
eleven numbers ([TODO.md § Etape 11](../TODO.md#etape-11--import-creature-bodies-as-gltf)). An editor that authors any
Grid shape and any placement is the day those rules are read again.

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

The User at the window sits outside that contract entirely. The User's window is an instrument — it shows the
same Grid from a free camera at a comfortable resolution, so the User can watch what a creature is doing and judge
whether the Grid is behaving. It is never an input to any creature.

The predecessor drew this line one notch further out, and the difference is a decision rather than drift. Its
documents forbade the engine any diagnostic display whatsoever while a creature was driving. Here the User may open a
window, because what it shows is *the Grid* and never the Program: a window onto the stage is an instrument, a window
onto the actor would be a leak. The rule about Programs is unchanged and is not negotiable — a Program that wants to
visualise its own state opens its own window, and the Grid gives it nothing.

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
pointing in several directions, or none at all, and still cost less than one frame of the User's window.

The User's window is separate and larger. It renders the same Grid with the same tracer, at a resolution the User
can actually look at.

### Hearing

The same acceleration structure that answers visual rays answers **acoustic rays**. Sound on the Grid travels along
the same geometry that light does — the same hierarchy, the same slab test, the same triangle intersection — but it
does not behave the same way once it arrives, and the differences are the interesting part.

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
2. **Programs first, Users later.** The Grid exists to be perceived by creatures. Today, User-facing output is a
   debugging convenience and is allowed to be crude; human avatars are [the long arc](#the-long-arc), taken in baby
   steps.
3. **Deterministic rendering.** Same Grid state in, same image out, on the same device and the same build. This is
   essential for *testing* and for *reproducing* a creature's behaviour, and it is worth being precise that it is not
   a training benefit: the guarantee is quantised to eight bits before anything reads it, and no learner can tell a
   sub-ulp radiance difference from the gradient noise it already lives in. What determinism actually buys is a
   refactor oracle, a host-against-device cross-check, and the ability to remove the Grid from the suspect list when
   a Program behaves strangely — which is worth a great deal, and is a different thing from what it is usually sold
   as. See [PROGRAM_INTERFACE.md](PROGRAM_INTERFACE.md) § Determinism and Replay.
4. **Honest embodiment.** A creature receives senses only. No scene graph access, no entity list, no ground truth.
5. **Creature-sized resolution.** Sensors render tiny. Only the User's window renders large.
6. **One Grid, two senses.** A single BVH, one triangle table and one material index serve visual and acoustic rays
   alike; the two coefficient sets are coupled by that index rather than packed into the same struct row.
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
point on, much of the interesting work happens in the Program repositories. What lies beyond the phases is
[the long arc](#the-long-arc), taken in baby steps.

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
