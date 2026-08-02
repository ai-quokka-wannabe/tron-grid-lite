# Architecture

Technical architecture of TronGrid Lite.

> This document describes the target architecture. Sections covering phases that are not yet implemented are marked
> accordingly. See `TODO.md` for the development journal, and [VISION.md](VISION.md) for the *why*.

---

## Overview

TronGrid Lite is a C++20 Vulkan 1.3 renderer for the Grid, a world inhabited by Programs. It is small on purpose. The
entire rendering strategy is one sentence: **trace a shallow, deterministic Whitted ray tree in ordinary compute
shaders, against a bounding volume hierarchy the project builds itself into storage buffers.**

There is no rasterised geometry pass in the final design, no acceleration-structure extension, and no denoiser. The
only rasterisation-adjacent work is a full-screen compute pass that writes the finished image into a swapchain image.

External dependencies are deliberately few:

| Dependency | Role |
|------------|------|
| Vulkan SDK 1.3+ | Headers, validation layers, tooling |
| Volk | Dynamic Vulkan function pointer loading |
| vulkan-hpp (`vk::raii`) | Type-safe C++ bindings and RAII ownership |
| Slang | Shader language, compiled ahead of time to SPIR-V |

Everything else — maths, windowing, logging, signals, the test harness, the BVH builder, the tracer — is in-house.

```text
┌──────────────────────────────────────────────────────────────────────┐
│                            Application                               │
│  Main loop · User camera · Grid geometry · Program hosting           │
├──────────────────────────────────────────────────────────────────────┤
│                            Renderer                                  │
│  BVH builder · Compute Whitted tracer · Post-process · Present       │
├──────────────────────────────────────────────────────────────────────┤
│                       Vulkan Backend (vk::raii)                      │
│  Instance · Device · Swapchain · Buffers · Images · Command buffers  │
├──────────────────────────────────────────────────────────────────────┤
│                        Internal Libraries (libs/)                    │
│  testing · signals · logging · math · bvh · window                   │
├──────────────────────────────────────────────────────────────────────┤
│                            Volk (loader)                             │
│  Dynamic Vulkan function pointer resolution, VK_NO_PROTOTYPES        │
└──────────────────────────────────────────────────────────────────────┘
          ▲
          │ senses out, actions in — no engine access
          ▼
┌──────────────────────────────────────────────────────────────────────┐
│  Program (DLL / SO) — separate repository, loose coupling            │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Key Design Decisions

| Decision | Choice | Why |
|----------|--------|-----|
| Vulkan loading | Volk, dynamic | No static link; `VK_NO_PROTOTYPES` defined globally by the build |
| Vulkan C++ bindings | vulkan-hpp `vk::raii` | RAII ownership; no manual `vkDestroy*` or `device.destroy*` anywhere |
| Rendering model | Compute only | No graphics pipeline, no `VkRenderPass`, no `VkFramebuffer`, no subpass bookkeeping |
| Ray tracing | Hand-written traversal in compute shaders | Reference GPU exposes zero ray-tracing extensions |
| Acceleration structure | Self-built BVH in storage buffers | Fully inspectable, portable, reusable for acoustics |
| Shading model | Whitted 1980, deterministic | Perfect mirrors plus emissive geometry need no Monte Carlo |
| Materials | One continuous, branchless parameter space | Mirror, neon and glass are named points in it, four values each |
| Shader language | Slang | Modern, modular, compiles to SPIR-V |
| Creature vision | Dedicated small render targets, 64 x 64 to 256 x 256 | Biologically honest; keeps ray counts trivial |
| Debug window output | Separate, larger swapchain window | Debugging and observation only |
| Acoustics | Same BVH, same surfaces | One Grid, two senses |
| Coordinate system | Right-handed, Y-up | Matches glTF and most authoring tools |
| Units | Metres | Physically meaningful light and sound propagation |
| Colour space | Linear internal, sRGB on output | Correct accumulation and blending |
| HDR range | 16-bit float | Emissive neon needs headroom well beyond 1.0 |
| Present mode | MAILBOX | Low latency, no tearing |
| Inter-system messaging | `SignalsLib::Signal<T>` | Thread-safe, lifetime-safe via `weak_ptr` |

---

## Internal Libraries (`libs/`)

The project is assembled from self-contained static libraries — bricks that snap together through CMake
`target_link_libraries`. Each has its own include directory, its own sources, and its own test suite.

```text
libs/
├── testing/    # TestingLib — TEST_CHECK, TEST_CHECK_EQUAL, TEST_CHECK_THROWS
├── signals/    # SignalsLib::Signal<T> — thread-safe typed message queues
├── logging/    # LoggingLib::Logger — background logging thread
├── math/       # MathLib — Vec2/3/4, Mat4, Quat, projection helpers (header-only)
├── bvh/        # BvhLib — binned-SAH builder and the flattened Node/Triangle arrays the GPU reads
└── window/     # WindowLib::Window — platform windowing (Win32 / XCB)
```

Rules:

- **PascalCase namespaces with a `Lib` suffix** — `TestingLib`, `SignalsLib`, `LoggingLib`, `MathLib`, `BvhLib`, `WindowLib`.
  They are general-purpose and could be extracted into their own repositories later.
- **Each library is self-contained** — its own `CMakeLists.txt`, its own `include/<lib>/` directory, its own `tests/`
  directory linking against `testing`.
- **Plain CMake target names** — `testing`, `signals`, `logging`, `math`, `bvh`, `window`.
- **Static libraries only**, except `math` and `signals`, which are header-only `INTERFACE` targets.
- **`testing` is the foundation brick** — every other library's tests link against it. No third-party test framework.

### Signal-Based Communication

`SignalsLib::Signal<T>` is a mutex-protected typed queue. It exists to carry messages **across a thread boundary**,
and that is the only place it belongs — a lock per message buys nothing when both ends run on the same thread.

**It has two users.** `LoggingLib::Logger` holds one as the queue its background writer drains, as a plain member,
because the logger owns both ends and outlives both. The second carries window events from the event thread to the
render thread, and it is the reason the renderer is threaded at all: on Win32 a modal resize drag runs the platform's
own message loop, so a renderer sharing that thread simply stops until the User lets go of the window edge.

The division of labour follows the platform's constraints rather than taste. The message pump belongs to the thread
that created the window, and `ShowCursor` is per-thread on Win32, so events and the cursor stay on the event thread.
Everything Vulkan is on the render thread, because a queue submission wants one owner.

Two details are worth stating, because both are easy to get wrong and neither is visible from the type:

- **The emit happens under the mutex the reader waits on.** A message queued after the reader has tested its
  predicate but before it has registered as a waiter would notify nobody. `Signal` is internally locked, so the queue
  itself is safe; the lost wakeup is a property of the condition variable beside it, not of the queue.
- **`Window::setEventCallback` is what makes the whole thing work.** It fires from inside the platform's own message
  handling, so it keeps feeding the queue during a modal drag — precisely when `pumpEvents` cannot run. The hook was
  written for this and sat unused until now.

Phase 5's acoustic solve, which runs at roughly a tenth of the visual rate, is the third user waiting behind these.

Direct calls remain the right answer for same-tick, same-system data access, and RAII members remain the right answer
for parent-child ownership such as device to swapchain.

---

## Vulkan Backend

### Ownership: `vk::raii` Everywhere

Every Vulkan object is owned by a `vk::raii` wrapper, which destroys its handle when it leaves scope.

```cpp
// Good — RAII, automatic and correctly ordered cleanup.
vk::raii::Device device = /* ... */;
vk::raii::Image image = device.createImage(image_info);
vk::raii::DeviceMemory memory = device.allocateMemory(alloc_info);

// Forbidden — manual lifecycle.
vk::Device raw_device = /* ... */;
vk::Image raw_image = raw_device.createImage(image_info);
// ... someone must remember raw_device.destroyImage(raw_image). Never do this.
```

**Rule: never use non-RAII vulkan-hpp types for ownership.** Non-RAII types are acceptable only as transient handles
passed to calls that do not transfer ownership.

Objects are stored as members, in `std::unique_ptr`, or in `std::vector`. Destruction order follows reverse member
declaration order, so struct layouts are planned with that in mind.

```text
vk::raii::Instance
  └── vk::raii::Device
        ├── vk::raii::SwapchainKHR
        ├── vk::raii::CommandPool
        │     └── vk::raii::CommandBuffer
        ├── vk::raii::Buffer          (BVH nodes, triangles, materials)
        ├── vk::raii::Image           (HDR target, sensor targets)
        ├── vk::raii::DeviceMemory
        ├── vk::raii::ImageView
        ├── vk::raii::DescriptorPool
        ├── vk::raii::Pipeline        (compute only, in the final design)
        ├── vk::raii::Semaphore
        └── vk::raii::Fence
```

### Loading: Volk

Vulkan is loaded dynamically through Volk. `VK_NO_PROTOTYPES` is defined globally by the build, so no Vulkan entry
point is ever resolved by the linker. A single translation unit provides the Volk implementation; `volkInitialize` runs
before instance creation and `volkLoadDevice` after device creation, so device-level calls bypass the loader trampoline.

### No Render Passes

The renderer is compute-only: there is no graphics pipeline, no `VkRenderPass` and no `VkFramebuffer` in this
codebase. Dynamic rendering is a Vulkan 1.3 core feature here rather than an extension — the only device extension
requested is `VK_KHR_swapchain` — and `src/device.cpp` still demands `dynamicRendering` of the physical device and
enables it alongside `synchronization2`, but nothing calls `vkCmdBeginRendering` today.

### Platform Layer

`WindowLib::Window` wraps Win32 on Windows and XCB on Linux, with no abstraction beyond what the two backends actually
share. Surface creation uses `VK_USE_PLATFORM_WIN32_KHR` or `VK_USE_PLATFORM_XCB_KHR`.

---

## The Grid and the BVH

### Geometry Representation

The Grid is a flat list of triangles in world space, plus a parallel list of material indices. There is no scene
graph, no instancing layer, no transform hierarchy in the GPU data. Geometry is small enough that flattening it is the
simplest thing that works.

### Material Model

There are no kinds of surface. Every surface is one perfectly smooth material that reflects, transmits and emits at
once, and a material record is four values:

| Parameter | Meaning |
|-----------|---------|
| `colour` | Tint applied to reflected and transmitted light. Usually near-black |
| `index_of_refraction` | Drives Fresnel everywhere, and Snell refraction where light passes through |
| `emission` | Radiance added on hit — this is the neon |
| `transmission` | How much non-reflected light passes through rather than being absorbed |

"Mirror", "neon" and "glass" are named points in that space rather than categories, so a glowing translucent surface
is as ordinary as any of them and the shader never branches on a material type.

There is no roughness, no metallic parameter, no microfacet distribution, no normal map, no texture of any kind. The
reflection direction is `reflect(d, n)`; the refraction direction is `refract(d, n, eta)` with total internal
reflection falling back to the reflection branch. That is the entire BRDF.

Each material record carries **optical properties only**, and deliberately so. Acoustic fields were reserved here
once and removed again: reserving two further std430 rows doubled the material buffer for fields nothing read, and a
field nothing reads is a field nothing maintains. Worse, the stale acoustic members outlived their removal in the
shader's copy of the struct and produced an out-of-bounds read that only GPU-assisted validation caught.

Phase 5 will therefore **have to put those values somewhere**, and that is the intended cost. They go in a **parallel
buffer indexed by the same material index**, not in a wider `Material`, for two reasons that both come out of the
history above.

The first is bandwidth, and it is the same argument that removed them. `Material` is exactly two std430 rows; two
more floats round up to three, half again as wide. The visual pass reads a material at every hit of every ray of
every pixel, and the acoustic pass re-solves at something closer to ten hertz — so widening the shared record makes
the hot path carry cold data on every fetch, forever, to spare the cold path one buffer binding.

The second is that a separate buffer cannot drift. The bug that made the first attempt memorable was the shader's
copy of `Material` still declaring acoustic members after the C++ side had dropped them, which only GPU-assisted
validation caught. Fields the visual shader never declares cannot come adrift from fields it never reads. The
`static_assert`s on `Material` in `src/components.hpp` stay exactly as they are, and keep meaning what they say.

### BVH Construction and Layout

The BVH is built on the CPU and uploaded into storage buffers. It is a plain binary BVH over axis-aligned bounding
boxes, built by binned surface-area-heuristic splitting, and flattened into a linear array so traversal needs only an
index and a small explicit stack.

```text
Descriptor set 0, as the tracer binds it:

  binding 0  output_image   rgba16f storage image, linear radiance, one thread per pixel
  binding 1  nodes          [ bounds_min, left_or_first, bounds_max, triangle_count ]
  binding 2  triangles      [ v0, material, edge1, padding0, edge2, padding1 ]
  binding 3  materials      [ colour, index_of_refraction, emission, transmission ]
```

Triangles carry no normal: every face is flat-shaded, so the shader derives the geometric normal from the two stored
edges.

Traversal in the compute shader is an ordinary iterative loop: test the ray against both child slabs, descend into
the nearer one immediately and push only the farther one when it was hit as well, pop until the stack empties. Leaf
nodes run Möller-Trumbore against their triangle range. Nothing exotic, nothing vendor-specific, and every step of it
is visible in a shader the author wrote.

The same buffers will be bound by the acoustic pass in Phase 5. The BVH is built once per frame at most — in practice
only when the Grid changes — and is shared by every sensor and by the debug view.

---

## The Compute Whitted Tracer

One compute shader does the entire image. Each invocation owns one pixel of one render target.

1. Generate the primary ray from the camera or sensor description in the push constants.
2. Traverse the BVH; find the closest hit.
3. If nothing was hit, return black. The void is genuinely empty — there is no sky to sample.
4. On a hit, add the surface's emissive radiance.
5. Spawn the deterministic continuation, unconditionally and with no branch on any material type: Schlick's Fresnel
   approximation decides the reflected share, `transmission` takes what it wants of the remainder, the transmitted
   branch is pushed onto the ray stack and the reflected branch is followed immediately.
6. Repeat to a fixed maximum depth, then terminate. The ray tree is shallow and bounded by construction.

Because there is no random sampling anywhere in this loop, the output is **noise-free and reproducible**. The same
Grid state and the same camera produce a bit-identical image. That is what makes a denoiser unnecessary and what makes
creature training runs reproducible.

The recursion is written as an explicit iterative loop with a small ray stack — compute shaders have no recursion, and
a bounded stack is cheaper and more predictable than one anyway.

---

## Render Targets: Sensors versus the Debug Window

Two distinct classes of render target exist, and they are not the same resource.

| | Creature sensor targets | Debug window |
|---|---|---|
| **Purpose** | The input a creature perceives | Debugging and observation for the User |
| **Resolution** | 64 x 64 to 256 x 256, per eye | Whatever the debug window is sized to |
| **Count** | One per eye, several eyes per creature | Exactly one |
| **Camera** | Rigidly attached to the creature | Free-flight debug camera |
| **Format** | `R16G16B16A16_SFLOAT`, read back or sampled by the Program | HDR, then post-processed and presented |
| **Post-processing** | None — creatures receive linear radiance | Bloom and tonemapping |
| **Present** | Never presented | Presented through the swapchain |

The debug view is not privileged in any way that matters: it runs the same tracer over the same BVH. It is simply
larger, and it is the only target that ever reaches a monitor.

Creature sensors are deliberately not tonemapped. A creature receives linear radiance, and any perceptual compression
is the Program's business, not the renderer's.

---

## Nothing Runs Without a Reason

**Every pass in this engine is driven by change, never by a clock.** The Grid is a world that mostly sits still,
watched through a window that is usually not moving, and the closest thing to it is a CAD viewport rather than a game
loop. Spinning the GPU at its maximum rate to redraw an image identical to the previous one is the most expensive
thing this program can do for no result — it holds a laptop GPU at full clock, with the fans and battery drain that
implies, to produce a picture nobody can tell from the last.

Measured on the reference machine, on the static Grid with the camera still: **0.09 s of CPU over 12 s of wall clock,
against 7.56 s for the same period on the build that drew unconditionally.** Roughly eighty times less, and the GPU
drops off its clocks entirely. The second figure is what holding a movement key still costs today, because a moving
camera draws every pass — the saving is only ever claimed for a Grid and a view that are both standing still.

The rule applies to both senses, but they earn it differently:

- **The debug view compares the state it drew from** — camera position, orientation, field of view, surface size — and
  sleeps on the render channel's condition variable when none of them has moved. State comparison rather than dirty
  flags, deliberately: a dirty flag is correct only if every writer remembers to raise it, and forgetting costs a
  window that has stopped updating. Comparing the state cannot be forgotten. The one thing this must never get wrong is
  a swapchain rebuild, whose new images hold undefined contents — a resize that left the camera and the size unchanged
  would otherwise look idle and leave garbage on screen, so a rebuild explicitly forces the next frame.
- **The acoustic gather has a stronger licence, because it is a pure function.** Same Grid, materials, ear and config
  gives a bit-identical response, so a solve whose inputs have not changed may be skipped outright — and the skipped
  answer is not an approximation of the real one, it *is* the real one. A stationary creature in a static Grid hears
  exactly what it heard last tick. Re-solving is not cheap-and-approximate, it is expensive-and-pointless. The cache
  key is the Grid's generation, the ear's position and the config, and `src/tests/acoustics_tests.cpp` pins that each
  of the three can change the answer — which is what makes them the key rather than a guess.

There is deliberately no flag to draw unconditionally. The GPU profiler averages over frames and a still camera
produces none, so a run of frames to average is obtained by holding a movement key — which is what a flag would have
done anyway, and is how every frame timing quoted in this repository was taken.

## Frame Flow

```text
                              ┌───────────────────────┐
                              │      Main loop        │
                              │  poll window events   │
                              │  step Grid state      │
                              └───────────┬───────────┘
                                          │
                              ┌───────────▼───────────┐
                              │   BVH build / update  │  CPU, only when
                              │   upload to SSBOs     │  geometry changed
                              └───────────┬───────────┘
                                          │
                   ┌──────────────────────┼──────────────────────┐
                   │                                             │
       ┌───────────▼────────────┐                    ┌───────────▼────────────┐
       │  Sensor trace pass     │                    │  Debug view trace pass │
       │  compute, 64 x 64 ...  │                    │  compute, window size  │
       │  one dispatch per eye  │                    │  same shader, same BVH │
       └───────────┬────────────┘                    └───────────┬────────────┘
                   │                                             │
       ┌───────────▼────────────┐                    ┌───────────▼────────────┐
       │  Sensor images (HDR)   │                    │  HDR colour target     │
       │  linear radiance,      │                    │  R16G16B16A16_SFLOAT   │
       │  no post-processing    │                    └───────────┬────────────┘
       └───────────┬────────────┘                                │
                   │                                 ┌───────────▼────────────┐
                   │                                 │  Bloom pass (compute)  │
                   │                                 │  bright extract + blur │
                   │                                 └───────────┬────────────┘
                   │                                             │
                   │                                 ┌───────────▼────────────┐
                   │                                 │  Tonemap pass          │
                   │                                 │  compute, sRGB encode  │
                   │                                 └───────────┬────────────┘
                   │                                             │
       ┌───────────▼────────────┐                    ┌───────────▼────────────┐
       │  Program               │                    │  Present (MAILBOX)     │
       │  DLL / SO, separate    │                    │  swapchain image       │
       │  repo, reads senses,   │                    └────────────────────────┘
       │  writes actions        │
       └───────────┬────────────┘
                   │
                   └───────────► back into Grid state, next tick
```

Both trace passes read the same BVH buffers in the same frame; they differ only in dispatch size and in the camera
description pushed into them. When Phase 5 lands, an acoustic trace pass joins the sensor branch, dispatching against
the same buffers and feeding a hearing sensor instead of an image.

### Frame Synchronisation

Double buffering with the standard triple of primitives:

- **Image-available semaphore** — the swapchain image is ready to be written.
- **Render-finished semaphore** — rendering into that image has completed.
- **In-flight fence** — the CPU may not queue work for a frame whose GPU work is still outstanding.

Compute-to-compute dependencies within a frame use `vk::ImageMemoryBarrier2` with
`eComputeShader`/`eShaderStorageWrite` to `eComputeShader`/`eShaderStorageRead`. Several of them carry a layout
transition as well: an image that the next pass overwrites completely is moved from `eUndefined` to `eGeneral`, which
discards the old contents for free and which a plain memory barrier could not express. There is no cross-frame state
to synchronise, because there is no temporal accumulation anywhere in the pipeline.

---

## Acoustic Rays *(Phase 5, planned)*

Sound is traced the same way light is, through the same structure:

- The same BVH buffers are bound to an acoustic compute pass.
- Rays are cast from a sound source, or gathered towards a listener, and traversal is bit-for-bit the same algorithm.
- On hit, nothing is applied to the surface's account at all: reflection is lossless. Path length accumulates into a
  delay, and a surface that sings deposits into the bin that delay selects.
- The output is an impulse response per band — where energy arrives in time — delivered to a creature's hearing
  sensor, not an image.

Nothing about the BVH, its buffer layout or the traversal code has to change: none of it is specific to light.

### One acoustic value, and specifically not absorption, scattering or transmission

The optical side asks two questions of a surface: what does it do to light that arrives, and what light does it
originate. `colour` and `emission` answer them. **The acoustic side answers only the second.** Every surface on the
Grid is a perfect acoustic mirror, so the acoustic table is one float per material — source strength — and there is
no acoustic material model at all beyond it.

Three things a room-acoustics engine would carry are absent, and each for its own reason:

- **Absorption** was modelled and then removed on its own numbers. At the authored `alpha = 0.02` of a polished hard
  surface, ten bounces cost 0.88 dB, and rays on an open plane escape after one or two — so the realistic figure is
  nearer 0.2 dB against spreading's 26 dB across the range cap. Treble's documentation puts the measurement
  uncertainty on such a coefficient at about ±0.2, which is larger than the effect it produced. A term whose error bar
  exceeds its value is decoration rather than physics.
- **Transmission** was never modelled. Sound does pass through a glass slab in reality, and representing that honestly
  needs a thickness, a transmission coefficient and an interface model — the acoustic counterpart of exactly the
  microfacet machinery the optical side does without. On the Grid a slab is an obstacle, and behind one there is quiet.
- **Scattering** is dropped because the floor's terraces already **are** the scattering. Their risers stand a metre
  proud and their steps are metres across, against a wavelength of roughly eleven centimetres at the hum's
  fundamental — geometry far larger than the wave, redirecting it specularly in genuinely varied directions. A
  statistical coefficient would model a second time, and less honestly, what the triangles are already doing.

A lossless reflector is only safe because the Grid is an **open half-space**: rays leave and do not come back, total
path is capped, and reflection order is capped, so nothing accumulates without bound. Enclose any part of the Grid —
a room, a tunnel, a lid over a terrace hollow — and perfect mirrors would ring forever. That is the condition under
which this has to be reopened, and it is a geometry decision rather than an acoustic one.

### Nothing on the Grid sounds continuously

**Every source is pulsed, one-shot or modulated. There is no steady tone anywhere.** The neon does not hum on a
continuous sine; it pulses. A creature vocalising emits a call and stops, as an animal does. A worm dragging itself
across the floor scrapes — sustained, but noisy and modulated by its own gait rather than held at a constant level.

The engineering reason this costs nothing to honour is that **the gather computes an impulse response, and a source's
behaviour in time is not part of it.** The histogram answers "if this source fired an impulse now, where in time does
its energy arrive" — a property of geometry alone. What a source actually does in time is an envelope multiplying
that response at delivery. So a pulse, a one-shot call and a scrape are the same object with different envelopes, and
none of them changes a line of the traversal.

The reason it *matters* is perceptual, and it is much stronger than the compute argument. **A continuous tone carries
almost no delay information.** Every arrival overlaps every other, and a creature receives a steady level with no
temporal structure to measure — the 1 ms bins and their 17.2 cm of range resolution would be describing something the
listener cannot extract. Onsets are what make a delay measurable, which is precisely why bats emit pulses rather than
tones. Pulsing the Grid is what makes the histogram worth computing.

It also produces the right asymmetry for free: a scraping worm is easy to *detect* and hard to *range*, because a
sustained noisy source has no sharp onset to measure from. That is physically correct, and it is an emergent property
of the model rather than something written into it.

That leaves the material at two extra floats rather than four. The fields removed once for being read by nothing
should come back only when something reads them, and a scattering coefficient would arrive with nothing to drive it
and no diffuse tail to consume it — which is precisely how the first pair came to be deleted.

---

## Program Sensor Interface *(Phase 6, planned)*

A Program is a shared library — DLL on Windows, SO on Linux — living in its **own repository** in the same
organisation. The renderer knows nothing about what is inside it.

The coupling is deliberately one-directional and narrow:

```text
┌────────────────────────────┐        ┌────────────────────────────┐
│  TronGrid Lite             │        │  Program (DLL / SO)        │
│                            │        │  separate repository       │
│  traces sensor targets ────────────►│  reads senses              │
│                            │ senses │                            │
│  reads actions ◄────────────────────│  writes actions            │
│                            │  iface │  (any architecture at all) │
└────────────────────────────┘        └────────────────────────────┘
```

A Program never links against renderer internals, never touches a Vulkan object, and never reads Grid state. It gets
senses and it returns actions. There is no entity list, no position query, no ground truth of any kind — see
[VISION.md](VISION.md) § The Inhabitants for why that boundary is not negotiable.

The User's debug window sits entirely outside this interface and is never an input to any Program.

---

## What This Architecture Does Not Contain

Each omission below is a design decision, and each one is what keeps the renderer readable and portable to modest GPUs.

| Absent | Why |
|--------|-----|
| Hardware ray tracing extensions | The reference GPU, a GTX 1650 Ti, exposes none of them. Compute traversal runs everywhere |
| Mesh shaders and meshlets | Same portability reason, and the tracer does not rasterise geometry at all |
| Bindless descriptor indexing | A handful of explicit bindings is easier to follow and has no capability requirement |
| GPU-driven indirect pipeline | Nothing here is draw-call bound; indirect machinery would be cost without benefit |
| Denoiser (SVGF, ReSTIR, temporal accumulation) | Deterministic Whitted shading produces no noise, so there is nothing to denoise |
| Textures, samplers, asset pipeline | Four analytic parameters per surface describe the whole Grid |
| Roughness, microfacets, full PBR | Perfect mirrors and emissive geometry are the aesthetic; anything more would blur it |
| Volumetric fog, terrain, skybox | The Grid is infinite black; a missed ray costs nothing |
| Third-party physics or audio libraries | The BVH already answers both, and in-house keeps the dependency list short |
| Rendergraph, component system, resource handles | Not enough passes or entity variety to justify the abstraction. Revisit only with a concrete second use case |

---

## Build System

CMake 3.16+ with Ninja Multi-Config. Five presets cover every supported compiler and platform combination:

| Preset | OS | Compiler |
|--------|----|----------|
| `windows-msvc` | Windows | MSVC (cl) |
| `windows-clang-cl` | Windows | Clang-CL (MSVC ABI) |
| `windows-mingw` | Windows | MinGW-w64 (GCC) |
| `linux-x11-gcc` | Linux | GCC |
| `linux-x11-clang` | Linux | Clang |

```bash
cmake --preset <name>
cmake --build build/<name> --config Debug
```

Slang shaders are compiled to SPIR-V ahead of time as part of the build. No runtime shader compilation.

---

## Directory Structure

```text
tron-grid-lite/
├── .claude/          ← project instructions for AI assistants
├── .github/          ← CI workflows, Vulkan SDK setup actions
├── docs/             ← extended documentation (you are here)
├── images/           ← recorded animations the README embeds
├── libs/             ← internal libraries
├── src/              ← application and renderer sources, Slang shaders
├── tools/            ← Python scripts that drive the renderer from outside the build
├── CMakeLists.txt    ← root build configuration
├── CMakePresets.json ← compiler and platform presets
├── .clang-format     ← formatting rules
└── .editorconfig     ← editor settings
```

---

## References

- Whitted, T. (1980). *An improved illumination model for shaded display*. CACM 23(6).
- Möller, T. and Trumbore, B. (1997). *Fast, minimum storage ray/triangle intersection*.
- Wald, I. (2007). *On fast construction of SAH-based bounding volume hierarchies*.
- [Vulkan Tutorial](https://vulkan-tutorial.com)
- [vkguide.dev](https://vkguide.dev)
- [Slang Shader Language](https://shader-slang.org)
