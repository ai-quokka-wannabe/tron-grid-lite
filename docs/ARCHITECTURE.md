# Architecture

Technical architecture of TronGrid Lite.

> This document describes the target architecture. Sections covering phases that are not yet implemented are marked
> accordingly. See `TODO.md` for the development journal, and [VISION.md](VISION.md) for the *why*.

---

## Overview

TronGrid Lite is a C++20 Vulkan 1.3 renderer for a world inhabited by AI creature agents. It is small on purpose. The
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
│  Main loop · Debug camera · Scene · Creature agent hosting           │
├──────────────────────────────────────────────────────────────────────┤
│                            Renderer                                  │
│  BVH builder · Compute Whitted tracer · Post-process · Present       │
├──────────────────────────────────────────────────────────────────────┤
│                       Vulkan Backend (vk::raii)                      │
│  Instance · Device · Swapchain · Buffers · Images · Command buffers  │
├──────────────────────────────────────────────────────────────────────┤
│                        Internal Libraries (libs/)                    │
│  testing · signals · logging · math · window                         │
├──────────────────────────────────────────────────────────────────────┤
│                            Volk (loader)                             │
│  Dynamic Vulkan function pointer resolution, VK_NO_PROTOTYPES        │
└──────────────────────────────────────────────────────────────────────┘
          ▲
          │ sensor buffers out, motor commands in — no engine access
          ▼
┌──────────────────────────────────────────────────────────────────────┐
│  AI creature brain (DLL / SO) — separate repository, loose coupling  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Key Design Decisions

| Decision | Choice | Why |
|----------|--------|-----|
| Vulkan loading | Volk, dynamic | No static link; `VK_NO_PROTOTYPES` defined globally by the build |
| Vulkan C++ bindings | vulkan-hpp `vk::raii` | RAII ownership; no manual `vkDestroy*` or `device.destroy*` anywhere |
| Rendering model | Dynamic rendering (`VK_KHR_dynamic_rendering`) | No `VkRenderPass`, no `VkFramebuffer`, no subpass bookkeeping |
| Ray tracing | Hand-written traversal in compute shaders | Reference GPU exposes zero ray-tracing extensions |
| Acceleration structure | Self-built BVH in storage buffers | Fully inspectable, portable, reusable for acoustics |
| Shading model | Whitted 1980, deterministic | Perfect mirrors plus emissive geometry need no Monte Carlo |
| Materials | Mirror, emissive, glass | Three kinds of surface, a few floats each |
| Shader language | Slang | Modern, modular, compiles to SPIR-V |
| Creature vision | Dedicated small render targets, 64 x 64 to 256 x 256 | Biologically honest; keeps ray counts trivial |
| Spectator output | Separate, larger swapchain window | Debugging and observation only |
| Acoustics | Same BVH, same surfaces | One world, two senses |
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
└── window/     # WindowLib::Window — platform windowing (Win32 / XCB)
```

Rules:

- **PascalCase namespaces with a `Lib` suffix** — `TestingLib`, `SignalsLib`, `LoggingLib`, `MathLib`, `WindowLib`.
  They are general-purpose and could be extracted into their own repositories later.
- **Each library is self-contained** — its own `CMakeLists.txt`, its own `include/<lib>/` directory, its own `tests/`
  directory linking against `testing`.
- **Plain CMake target names** — `testing`, `signals`, `logging`, `math`, `window`.
- **Static libraries only**, except `math`, which is a header-only `INTERFACE` target.
- **`testing` is the foundation brick** — every other library's tests link against it. No third-party test framework.

### Signal-Based Communication

Systems that do not need to know about each other communicate through `SignalsLib::Signal<T>`, a thread-safe typed
queue. The ownership rule is:

- The **receiver owns** the signal, as `std::shared_ptr<SignalsLib::Signal<T>>`.
- The **sender holds** a `std::weak_ptr` to it.

When the receiver dies the weak pointer expires and the sender simply stops emitting. No dangling pointers, no manual
unregistration. Window resize events, log messages, and render-thread wake-ups all travel this way.

Direct calls remain the right answer for same-tick, same-system data access; RAII members remain the right answer for
parent-child ownership such as device to swapchain.

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

### Dynamic Rendering

There is no `VkRenderPass` and no `VkFramebuffer` in this codebase. Any graphics work — in practice only the early
phases and any debug wireframe drawing — uses `vkCmdBeginRendering` with attachments specified inline, and pipelines
declare their attachment formats through `vk::PipelineRenderingCreateInfo`.

| Traditional | TronGrid Lite |
|-------------|---------------|
| Pre-declared `VkRenderPass` with subpasses | No render pass object at all |
| `VkFramebuffer` per swapchain image | No framebuffer object at all |
| `vkCmdBeginRenderPass` | `vkCmdBeginRendering` with `vk::RenderingInfo` |
| Pipeline needs render pass and subpass index | Pipeline needs `vk::PipelineRenderingCreateInfo` |

### Platform Layer

`WindowLib::Window` wraps Win32 on Windows and XCB on Linux, with no abstraction beyond what the two backends actually
share. Surface creation uses `VK_USE_PLATFORM_WIN32_KHR` or `VK_USE_PLATFORM_XCB_KHR`.

---

## The Scene and the BVH

### Geometry Representation

The world is a flat list of triangles in world space, plus a parallel list of material indices. There is no scene
graph, no instancing layer, no transform hierarchy in the GPU data. Geometry is small enough that flattening it is the
simplest thing that works.

### Material Model

Exactly three kinds of surface exist. A material record is a handful of floats:

| Material | Optical behaviour | Parameters |
|----------|-------------------|------------|
| Mirror | Perfect specular reflection, no diffuse term | Single colour, usually near-black |
| Emissive | Mirror, plus radiance added on hit | Colour, emissive colour, emissive strength |
| Glass | Snell refraction plus Fresnel-weighted reflection | Colour tint, index of refraction |

There is no roughness, no metallic parameter, no microfacet distribution, no normal map, no texture of any kind. The
reflection direction is `reflect(d, n)`; the refraction direction is `refract(d, n, eta)` with total internal
reflection falling back to the reflection branch. That is the entire BRDF.

Each material record also carries **acoustic properties** alongside the optical ones — an absorption coefficient and a
scattering coefficient — so that Phase 5 can trace sound through the same data without touching the layout again.

### BVH Construction and Layout

The BVH is built on the CPU and uploaded into storage buffers. It is a plain binary BVH over axis-aligned bounding
boxes, built by binned surface-area-heuristic splitting, and flattened into a linear array so traversal needs only an
index and a small explicit stack.

```text
Storage buffers consumed by the tracer:

  binding 0  bvh_nodes      [ aabb_min, aabb_max, left_or_first_tri, count ]
  binding 1  triangles      [ v0, v1, v2, normal, material_index ]
  binding 2  materials      [ colour, emissive, ior, absorption, scattering, flags ]
```

Traversal in the compute shader is an ordinary iterative loop: test the ray against the node's slab, push the two
children in front-to-back order, pop until the stack empties. Leaf nodes run Möller-Trumbore against their triangle
range. Nothing exotic, nothing vendor-specific, and every step of it is visible in a shader the author wrote.

The same buffers will be bound by the acoustic pass in Phase 5. The BVH is built once per frame at most — in practice
only when the world changes — and is shared by every sensor and by the spectator view.

---

## The Compute Whitted Tracer

One compute shader does the entire image. Each invocation owns one pixel of one render target.

1. Generate the primary ray from the camera or sensor description in the push constants.
2. Traverse the BVH; find the closest hit.
3. If nothing was hit, return black. The void is genuinely empty — there is no sky to sample.
4. On a hit, add the surface's emissive radiance.
5. Spawn the deterministic continuation:
    - **Mirror** — one reflection ray.
    - **Emissive** — one reflection ray, since emissive surfaces are mirrors that also glow.
    - **Glass** — one refraction ray and one reflection ray, weighted by Schlick's Fresnel approximation.
6. Repeat to a fixed maximum depth, then terminate. The ray tree is shallow and bounded by construction.

Because there is no random sampling anywhere in this loop, the output is **noise-free and reproducible**. The same
world state and the same camera produce a bit-identical image. That is what makes a denoiser unnecessary and what makes
creature training runs reproducible.

The recursion is written as an explicit iterative loop with a small ray stack — compute shaders have no recursion, and
a bounded stack is cheaper and more predictable than one anyway.

---

## Render Targets: Sensors versus Spectator

Two distinct classes of render target exist, and they are not the same resource.

| | Creature sensor targets | Spectator window |
|---|---|---|
| **Purpose** | The input a creature perceives | Human debugging and observation |
| **Resolution** | 64 x 64 to 256 x 256, per eye | Whatever the debug window is sized to |
| **Count** | One per eye, several eyes per creature | Exactly one |
| **Camera** | Rigidly attached to the creature | Free-flight debug camera |
| **Format** | `R16G16B16A16_SFLOAT`, read back or sampled by the brain | HDR, then post-processed and presented |
| **Post-processing** | None — creatures receive linear radiance | Bloom and tonemapping |
| **Present** | Never presented | Presented through the swapchain |

The spectator view is not privileged in any way that matters: it runs the same tracer over the same BVH. It is simply
larger, and it is the only target that ever reaches a monitor.

Creature sensors are deliberately not tonemapped. A creature receives linear radiance, and any perceptual compression
is the brain's business, not the renderer's.

---

## Frame Flow

```text
                              ┌───────────────────────┐
                              │      Main loop        │
                              │  poll window events   │
                              │  step world state     │
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
       │  Sensor trace pass     │                    │  Spectator trace pass  │
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
       │  Creature brain        │                    │  Present (MAILBOX)     │
       │  DLL / SO, separate    │                    │  swapchain image       │
       │  repo, reads sensors,  │                    └────────────────────────┘
       │  writes motor commands │
       └───────────┬────────────┘
                   │
                   └───────────► back into world state, next frame
```

Both trace passes read the same BVH buffers in the same frame; they differ only in dispatch size and in the camera
description pushed into them. When Phase 5 lands, an acoustic trace pass joins the sensor branch, dispatching against
the same buffers and feeding a hearing sensor instead of an image.

### Frame Synchronisation

Double buffering with the standard triple of primitives:

- **Image-available semaphore** — the swapchain image is ready to be written.
- **Render-finished semaphore** — rendering into that image has completed.
- **In-flight fence** — the CPU may not queue work for a frame whose GPU work is still outstanding.

Compute-to-compute dependencies within a frame use `vk::MemoryBarrier2` with
`eComputeShader`/`eShaderWrite` to `eComputeShader`/`eShaderRead`. There is no cross-frame state to synchronise,
because there is no temporal accumulation anywhere in the pipeline.

---

## Acoustic Rays *(Phase 5, planned)*

Sound is traced the same way light is, through the same structure:

- The same BVH buffers are bound to an acoustic compute pass.
- Rays are cast from a sound source, or gathered towards a listener, and traversal is bit-for-bit the same algorithm.
- On hit, the material's **acoustic** absorption and scattering coefficients are applied instead of its optical
  colour, and path length accumulates into a delay.
- The output is a small set of arrival events — direction, delay, attenuation — delivered to a creature's hearing
  sensor, not an image.

This works only because the material record was designed from the start to carry both sets of properties. Nothing about
the BVH, the buffer layout, or the traversal code needs to change.

---

## AI Agent Sensor Interface *(Phase 6, planned)*

A creature brain is a shared library — DLL on Windows, SO on Linux — living in its **own repository** in the same
organisation. The renderer knows nothing about what is inside it.

The coupling is deliberately one-directional and narrow:

```text
┌────────────────────────────┐        ┌────────────────────────────┐
│  TronGrid Lite             │        │  Creature brain (DLL / SO) │
│                            │        │  separate repository       │
│  traces sensor targets ────────────►│  reads sensor buffers      │
│                            │ sensor │                            │
│  reads motor commands ◄─────────────│  writes motor commands     │
│                            │  iface │  (any architecture at all) │
└────────────────────────────┘        └────────────────────────────┘
```

The brain never links against renderer internals, never touches a Vulkan object, and never reads world state. It gets
sensor buffers and it returns motor commands. There is no entity list, no position query, no ground truth of any kind —
see [VISION.md](VISION.md) § The Inhabitants for why that boundary is not negotiable.

The human spectator window sits entirely outside this interface and is never an input to any brain.

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
| Textures, samplers, asset pipeline | Three analytic materials describe the whole world |
| Roughness, microfacets, full PBR | Perfect mirrors and emissive geometry are the aesthetic; anything more would blur it |
| Volumetric fog, terrain, skybox | The world is infinite black; a missed ray costs nothing |
| Third-party physics or audio libraries | The BVH already answers both, and in-house keeps the dependency list at four |
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
├── libs/             ← internal static libraries
├── src/              ← application and renderer sources, Slang shaders
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
