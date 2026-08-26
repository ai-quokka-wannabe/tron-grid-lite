# Architecture

Technical architecture of TronGrid Lite.

> This document describes the architecture as built. Where the plan and the build diverged, the
> section says so in place. See `TODO.md` for the roadmap and open etapes, `CHANGELOG.md` for what changed and why,
> and [VISION.md](VISION.md) for the *why* behind the project.

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
| The User's window output | Separate, larger swapchain window | Observation: the live view of the world, or the stage alone under `--debug` |
| Acoustics | Same BVH, same surfaces | One Grid, two senses |
| Coordinate system | Right-handed, Y-up | Matches glTF and most authoring tools |
| Units | Metres | Physically meaningful light and sound propagation |
| Colour space | Linear internal, sRGB on output | Correct accumulation and blending |
| HDR range | 16-bit float | Emissive neon needs headroom well beyond 1.0 |
| Present mode | MAILBOX | Low latency, no tearing |
| Inter-system messaging | `SignalsLib::Signal<T>` | A mutex-protected typed queue, and nothing more; lifetime is the owner's problem |

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
- **Each library is self-contained** — its own `CMakeLists.txt`, its own `include/` directory holding exactly one
  subdirectory, and its own `tests/` directory linking against `testing`.

  That subdirectory is what consumers spell in an `#include`, and it is **always the target name**: `signals`
  publishes `signals/`, `logging` publishes `logging/`, and so on for all six. A header is exactly where its target
  name says it is, and the next library added holds to the same rule.
- **Plain CMake target names** — `testing`, `signals`, `logging`, `math`, `bvh`, `window`.
- **Static libraries only**, except `math` and `signals`, which are header-only `INTERFACE` targets.
- **`testing` is the foundation brick** — every other library's tests link against it. No third-party test framework.

**The admission criterion is a test, and it excludes the renderer permanently.** An entry must be
general-purpose, extractable into its own repository, and testable through its own `tests/` directory
linking against `testing`. Anything holding `Device`, `Swapchain`, `Tracer` or `PostProcess` fails
both halves, and amending the criterion to let it in would leave `libs/` meaning only "code we
compiled separately" — which would hand `libs/gltf` a tier with no admission test at the moment it
arrives.

**If `src/` is ever split, the cut is at the Vulkan boundary and nowhere else.** The GPU-free unit
tests link only `math`, `bvh` and `testing`, and need neither the Vulkan SDK nor XCB; linking them
against a target that also contains `volk.cpp`, `device.cpp` and `tracer.cpp` would put Vulkan and
XCB include directories and preprocessor definitions onto the compile line of the only GPU-free tests
in the repository. That rules out a single whole-of-`src` target as firmly as the criterion above
rules out `libs/render`.

The split, when a trigger fires, is two plain lowercase targets **in `src/`** — `render` and `sim` —
with no `Lib` namespace suffix, the absence of the suffix being the deliberate signal that these are
not extractable. Three preconditions are hard rather than stylistic:
`VULKAN_HPP_NO_STRUCT_CONSTRUCTORS`, `VK_NO_PROTOTYPES` and `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1`
must become `PUBLIC` on the Vulkan target — leaving them `PRIVATE` is the ODR hazard
`src/CMakeLists.txt` already names — the source directory must become `PUBLIC` on both, and the Slang
rules must stay on the executable, because `main.cpp` resolves `.spv` paths against the executable's
own directory.

Three names are disqualified rather than merely unattractive. **`grid`** is spent twice over:
`world.cpp` is the Grid's geometry and would land in the other target, and `geometry.hpp` already
spends the word on the floor mesh. **`TronGridEngineLib`** asserts a lineage this project is only
permitted to describe as "reuses code from", in every `#include` and every qualified name, and it is
the one naming mistake here that is genuinely hard to walk back. **`TronGridLiteEngineLib`** fails
three ways: no `libs/` entry carries the project name, "engine" is already spent one layer up where
this document uses it to mean the renderer as a whole, and twenty-one characters stands against a
convention of three to seven.

The open include-subdirectory question above should be settled in the same commit as any split — the
published subdirectory matches the target name, with `signal/` and `log/` grandfathered — because
`libs/gltf` inherits whatever precedent is standing when it lands.

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

**A third user was predicted here and never arrived, which is worth keeping rather than deleting.** The
prediction was that Phase 5's acoustic solve would want a queue of its own. Phase 5 shipped without one:
`Acoustics::gather` is a synchronous pure function whose answer the caller needs in hand before it can do
anything else, so there is nothing for a queue to decouple. What makes it cheap is that it can be
*skipped* — a pure function of unchanged inputs — not that it can be deferred.

The general lesson is the one this repository keeps relearning: a queue is for crossing a thread
boundary, and a boundary is not created by a subsystem being slow. It is created by something being
unable to wait, which so far is true of exactly one thing here — the platform's event callback during a
modal drag.

**The rule, stated as a rule rather than as a habit: a `Signal<T>` may carry only payloads whose
consumer's output dies with the process** — pixels the User sees, a line on stdout, a validation
verdict. Nothing whose output is read by the next tick may travel through one. Inside the tick every
edge is a direct synchronous call.

The test is not "does the consumer accumulate?" — the render thread sums mouse deltas across however
many messages landed in one drain, and is safe anyway because nothing downstream ever reads its
camera back. The test is whether the consumer's output is read by the next tick.

**The corollary is the half that gets forgotten: the number of times a consumer runs must never be an
input to what it computes.** That forbids gating the simulation tick on `cv.wait(!queue.empty())`,
and equally forbids running a tick once per render pass.

FIFO is guaranteed **per producer**. With concurrent producers the interleaving is a function of
thread timing, so no value whose computation depends on drain order may travel through a Signal. That
sentence belongs in the header beside the lifetime paragraph, because it is the only thing standing
between a future careless `emit` and the replay guarantee.

A short list of things `Signal` will not grow, each because no user exists:

- **No blocking `waitAndConsume`,** despite two present-tense users each pairing a Signal with an
  external mutex and condition variable and documenting the identical lost-wakeup hazard. Neither
  could adopt it: `Logger::flush` waits for the queue to *drain*, and a primitive that blocks until a
  message arrives and then consumes it is the opposite of what a drain needs; and the render loop's
  predicate becomes compound the moment a roster exists. The wrapper collapse is illusory.
- **No bounded capacity, no close state, no move-emit.** Shutdown already has two working answers: an
  in-band `Stop` on the forward path, and a `jthread` stop token for the logger.
- **No `SnapshotReady{generation, slot}`.** A slot index through an unbounded unacknowledged queue is
  a use-after-overwrite: dropping is forbidden, blocking the producer is forbidden, so the producer
  laps the ring while the reader is stalled in `waitForFences` or a swapchain rebuild. If snapshots
  ever cross a thread they cross by value or by an owned buffer, never by index.
- **No second waiter on the render channel's condition variable.** `notify_one` is correct only
  because exactly one thread ever waits on it, and that is written down in the code for the same
  reason it is written down here.
- **Nothing that does not cross a thread gets a Signal** — profiler results included. `WindowLib`
  keeps a plain `std::queue`; do not upgrade it.

Two invariants on the render side are currently enforced by file layout rather than by a type, and
anything that gathers these objects into one owner must re-establish them deliberately. **The render
thread must be unable to name the Window** — asking it anything races the thread that owns it, and
today the only thing preventing that is that `runRenderLoop` does not take one. **Shutdown ordering
becomes member-declaration order** the moment the channel, the window and the logger are siblings, so
the reason must be written beside the member list rather than left in the guard that currently states
it. And the deterministic entry points construct no channel and start no thread; keeping that visible
in an entry point's signature is worth more than a comment saying it.

The Grid's own boundary is the same rule seen from the other side. The senses-to-Program-to-actions
path is a direct call and can never be a Signal: the plugin boundary is C99 and `Signal<T>` is a C++
template holding a `std::mutex`. GPU-to-host readback completion is signalled by a Vulkan fence,
which is what the recording path already does and what Phase 6 will copy. And creature actions never
flow back into physics through a queue — each call writes its own `TglActions` slot in an array
indexed by creature id, the Grid joins, and actions apply in roster order.

Direct calls remain the right answer for same-tick, same-system data access, and RAII members remain the right answer
for parent-child ownership such as device to swapchain.

---

## The threads, and the order they stop

Every thread this process ever starts, who owns it, what it may touch, and how it ends - written
down because a thread nobody named is a race nobody looked for. Adopted from the owner's
StringWiggler, whose ARCHITECTURE carries the same two sections for the same reason.

| Thread | Started by | Owns | Talks through | Stops when |
|---|---|---|---|---|
| **Event** (the process's main thread) | the OS | the window, the cursor, the message pump; in `--program` mode the whole host loop | `RenderChannel` (a `Signal`) to the render thread; the mixer's lock to the audio thread | the window closes, `--ticks` runs out, or a fatal error |
| **Render** | `main` in `--window`/`--debug`/`--replay`, one `std::thread` | everything Vulkan: device, swapchain, world, tracers, post-process; the world rebuild under `world_mutex` | reads the `RenderChannel`; publishes the camera to the mixer | `RenderThreadGuard` sends `Stop` and joins - on every exit path, error paths included |
| **Audio output** | `AudioLib::Output` (WASAPI on Windows, a silent drain elsewhere), one `std::thread` | the endpoint; nothing else | pulls from the mixer under its lock, event-driven | `~Output` sets `stopping` and joins |
| **Logger** | `LoggingLib::Logger`, one `std::jthread` | the sink | the log `Signal` | the stop token, joined in `~Logger` |
| **The Program's own** | the Program library, if it chooses (rc-worm's panel will) | its own affairs | nothing of the Grid's: `program_tick` runs on the event thread and must never block | the Program's business, before `library_shutdown` returns |

Three rules the table enforces:

- **A queue is for crossing a thread, and nothing crosses a thread without one** -
  § Signal-Based Communication. Values cross by value or by an owned buffer, never by index into
  something the other thread may resize.
- **The world rebuild is the one place two threads meet on Vulkan objects**, and it happens
  under `world_mutex` with the device idle: a REZ or DEREZ replaces the `World` and its tracers
  whole, never edits them in place.
- **The tick never waits for a window.** In `--program` mode there is no render thread at all;
  the host loop polls the wire, ticks the mind, and sends - a mind that thinks slowly delays
  its own intent (`Host::act` tags for the tick the world will step next), never the world.

**Shutdown, in order**, which is reverse construction and always join-before-destroy:

1. The loop ends (close, `--ticks`, or an exception unwinding through `main`).
2. `RenderThreadGuard`: detach the window callback (so a close arriving now calls nothing),
   send `Stop`, join the render thread. Inside a catch-all, because it runs on every path.
3. The device waits idle; the Vulkan objects are destroyed in reverse order by `vk::raii`.
4. `Host`/`Client` close the wire: BYE first, then the farewell drain, so a leave is a leave.
5. `~Output` stops and joins the audio thread; `~Mixer` follows.
6. `~Logger` last - everything above may still log while it stops - via the stop token.

Each step is idempotent and each destructor is `noexcept` in fact, not only in signature: the
one place a failure to stop cleanly could be reported is the handler at the bottom of `main`,
and a throw from a destructor would abort past it.

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

### Two mechanisms this backend is missing

Both are named here because each replaces a convention with something a caller cannot bypass, which
is the standard this repository holds itself to elsewhere.

**A type that owns `Tracer` and `PostProcess` together.** `PostProcess` stores non-owning image views
that `Tracer` owns and reallocates, holds no pointer back to `Tracer`, and cannot be notified. A
`Tracer::resize` without its matching `PostProcess::resize` leaves stale views in live descriptor
sets, and nothing in the program can detect it. The pairing is hand-written at four separate sites,
one of them behind a condition, and a free helper taking both by reference would only be a second
convention, because a caller can still reach `tracer.resize()` alone. A type that owns both and makes
the dangerous accessor unreachable is a mechanism.

**A submitter owning one command pool, N buffers and N fences.** Four sites hand-write the same
pool-plus-buffer-plus-fence-plus-timeout-loop sequence. One trap makes this worth stating rather
than leaving to a future reader to discover: **the pool creation flag is not uniform across those
sites** — two use `eTransient` and two use `eResetCommandBuffer` — so any helper unifying them must
take the flag as a parameter. Collapsing them to one flag would silently change a driver hint at two
sites, and a driver-hint change is invisible to a rendered-image digest.

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

Hearing therefore **had to put its value somewhere**, and it went into a **parallel buffer indexed by the same
material index** rather than into a wider `Material`, for two reasons that both come out of the history above. In the
event there is only one such value — source strength — because acoustically every surface is a perfect mirror, so the
parallel table is six floats.

The first is bandwidth, and it is the same argument that removed them. `Material` is exactly two std430 rows; even one
more float rounds up to three, half again as wide. The visual pass reads a material at every hit of every ray of
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

The same buffers are bound by the acoustic pass, unchanged. The BVH is built once per frame at most — in practice
only when the Grid changes — and is shared by every sensor and by the User's view.

### One hierarchy today, two when creatures move

**Decision: the Grid keeps one hierarchy until bodies exist, and gains a second level rather than a
faster builder when they do.** This was written down before Phase 6 because it decides the shape of
two etapes, and because the obvious optimisation is the wrong one.

**Built.** The second level exists on both sides — `BvhLib::Scene` and `intersectScene` on the host,
`traceScene` in `grid_bvh.slang` — and the Grid is an instance at the identity rather than a special
case, so the path a body will take is the path every frame already takes. The measurements below are
what argued for it and are kept because they are what a future change must argue against.

The Grid's hierarchy is built once at start-up and never touched again, which is correct for geometry
that cannot move. A creature can. The naive extension — put the bodies in the same hierarchy and
rebuild it every tick — has an obvious cost, and the obvious response is to make the builder faster.
Both were measured before either was believed.

Rebuild cost against triangle count, on the reference machine, at twenty repetitions taking the best.
The harness is synthetic — a deterministic scatter rather than the real Grid — and calibrates against
it closely: 16.1 ms here for 24,952 triangles against 14.4 ms measured in the renderer itself.

| Rebuilt every tick | Triangles | Cost |
|--------------------|----------:|-----:|
| The Grid alone | 24,952 | 16.1 ms |
| The Grid and five bodies | 30,072 | 19.7 ms |
| The Grid and twenty bodies | 45,432 | **31.0 ms** |
| The Grid and forty bodies | 66,392 | 48.8 ms |

Against a top level built over one box per object, rebuilt every tick:

| Objects in the top level | Cost |
|--------------------------|-----:|
| 2 | 0.0001 ms |
| 8 | 0.0006 ms |
| 20 | **0.0031 ms** |
| 200 | 0.0584 ms |

**Twenty creatures: 31 ms against 0.0031 ms, a factor of ten thousand.** And the 31 ms is spent
rebuilding the Grid's own 24,952 triangles, which did not move, twenty-five times a second.

#### Why this deletes the etape that was going to fix it

Etape 10 proposed parallelising the builder. Sixteen cores might turn 31 ms into something near 3 ms
— a real gain, and still a thousand times worse than not rebuilding at all, achieved by occupying
every core on the machine to recompute a structure that did not change. **Parallelising it would have
been a good solution to a problem that should not exist.**

That is the whole argument for writing the decision down first. The 31 ms is real, the instinct to
make it faster is reasonable, and it is the wrong instinct.

#### What the second level costs

Not nothing, and the cost is in the shader rather than on the host. A ray must be transformed into
each instance's local frame before descending into that instance's hierarchy, which means an inverse
transform per instance tested and an outer traversal wrapped around the inner one. Rays that only
ever meet the Grid — the great majority — pay one extra level of descent.

Two properties make that affordable:

- **A rigid body's hierarchy is built once and never again.** Only its transform changes per tick,
  which is why the per-tick cost collapses to the top level alone. This is the same reason
  ray-tracing hardware separates a top-level structure from bottom-level ones, and building it by
  hand is the same exercise this repository already performs for the single-level case.
- **The Grid is one instance among a handful.** The top level holds the Grid's box plus one per
  segment of every body — a chain of eight is eight boxes sharing one hierarchy — so it is a
  structure over tens of objects, not over fifty thousand triangles. `TODO.md`'s seventh question
  names the count at which the linear sweep stops being free.

**Measured with `--benchmark`, which reports the GPU's own timestamps per pass.** Sixty frames at
1280×720 on the GTX 1650 Ti after ten warm-up frames, best of three runs, the two paths differing only
in whether `trace.slang` calls `traceScene` or the single-level `trace`:

| Trace pass | Best of three | Spread |
|------------|--------------:|-------:|
| One level | 3.356 ms | 1.8% |
| Two levels | 3.342 ms | 2.4% |

**The second level costs nothing measurable at one instance** — the two overlap, and the 0.4%
separating them points the wrong way to be real. That is what the mechanism predicts: one extra slab
test and two affine transforms per ray segment, against a descent through a tree of depth 17 over
24,952 triangles. Under one per cent is where it should land, and under one per cent is where the
instrument stops being able to see it.

The whole frame is 3.7 ms, so 270 frames a second at 1280×720 on the reference GPU, tracing only.

Two notes on how to measure this, both learned by getting it wrong first:

- **Do not measure through `--record`.** Most of its wall clock is writing PPM files, which buries the
  pass under a run-to-run spread of ten per cent. That measurement showed a difference of a tenth of a
  per cent and could not have detected one ten times larger.
- **Interleave runs, and distrust a result with no mechanism whichever way it points.** Run
  back-to-back rather than alternated, the two-level binary appeared **7% faster** three times in a
  row. It was warm-up. Nothing about adding a level of indirection makes tracing faster, and that
  alone should have been enough to hold the result — but it is worth noticing that the same
  measurement 7% in the other direction would have looked exactly like the honest cost of the feature.

The picture itself is unchanged to the byte on both GPUs, which is the stronger statement: the same
rays reach the same triangles, so nothing above was traded for the flexibility.

#### The case that is not free, and what it implies

If bodies are **skinned** rather than rigid, their vertices move relative to each other and their
hierarchies must be rebuilt every tick: about 0.45 ms for a thousand-triangle body, so roughly 9 ms
for twenty of them. Still three times better than rebuilding everything, but no longer free.

That connects the two open questions in Etape 11 rather than leaving them independent. **Rigid
segments driven by the Grid's own physics make the two-level structure nearly free; skinned meshes
make it merely much better.** A world whose entire aesthetic is flat-shaded facets has little use for
smooth skinning, so the cheap answer and the fitting answer are the same one — which is the most
comfortable position an argument like this can end in, and worth being suspicious of exactly for that
reason.

#### The same hierarchy answers contact

The terrain half of this argument resolved the other way when physics shipped: ground contact runs
against the closed-form `gridMeshHeight` through the `GroundFunction` seam, because an analytic
ground gives contacts that replay exactly and devicelessly, which the world server's endgame
demands. What follows stands as the argument behind the contact model Master Control then built
(hulls, exact riser crossings, the separating-axis pass between creatures) and the two
load-bearing properties at the end are load-bearing still.

**One Grid, three senses if touch counts as one.** Nothing in the hierarchy, its layout or its
traversal changes to serve physics, and that is not a coincidence — `Triangle` already stores `v0`,
`edge1` and `edge2`, which are exactly the arguments a closest-point-on-triangle routine takes, with
no conversion. `intersectTriangle` is two-sided, which physics wants and vision does not care about.
Zero direction components are already replaced with a tiny constant, so a straight-down support probe
is already protected from the `0 * inf` NaN. This is the strongest form of the argument this document
makes for owning the BVH rather than importing a physics library: the structure was general because
nothing in it was ever specific to light.

Two properties of the existing code are load-bearing for physics and easy to undo by tidying:

- **The instance-space ray is left unnormalised**, so a distance found in instance space is already
  the world distance. A step trace's hit distance is therefore *directly* the fraction of the
  commanded step that may be taken, with no rescaling anywhere.
- **`build()` takes the triangle array by value and reorders it.** Any adjacency the physics needs —
  active-edge classification, so a capsule does not catch on the shared edge between two coplanar
  floor facets and stop dead on flat ground — must be computed **before** the build, from the
  generator that knows it for free. Recovering it afterwards from a triangle soup does not, and a
  terraced floor of flat-shaded facets is close to the worst case for the failure it prevents.

The queries themselves are additions rather than changes: a point-to-AABB squared-distance descent
(cheaper than the slab test, and with no reciprocal, so the zero-direction substitution and its NaN
hazard leave the physics path entirely), closest-point-on-triangle, and a contact collector.

**They return all contacts in range, never the nearest.** A body wedged where the floor meets a
pillar touches two faces, and taking only the nearest makes the solver alternate between their
normals across substeps, which is the jitter failure mode; when a fixed-capacity buffer overflows it
evicts the *farthest*, so the surviving set is chosen by distance rather than by SAH node order.
Contacts whose normals are within a couple of degrees are merged before solving, and that merge is
not deferrable polish: jitter here is not cosmetic, because an oscillating position produces
oscillating senses that a learning Program will chase as signal, and determinism is no defence — the
jitter is perfectly reproducible and still poisonous.

**Contact and friction are solved in the same pass.** Any arrangement that lifts a body onto a
surface and then pushes it laterally chatters forever on every ledge, and the chatter arrives in
`touch_*` as a periodic signal produced by nothing in the world.

**Exactly one signature changes: an `exclude_instance` parameter on the host `intersectScene`**,
because every physics probe starts inside the body that cast it. It is host-only — the shader casts
no physics probe — and that asymmetry is stated here deliberately, because otherwise it reads later
as drift between the two copies of the traversal.

**`gridMeshHeight` is a bound seed and never an answer.** The vertical distance to the mesh is a
valid upper bound on the distance to the nearest surface inside the floor extent, because the point
directly below is itself on the mesh, and seeding the traversal with it makes the dominant case fast.
Outside the floor's extent the height field clamps to the edge, so the bound becomes a lie and the
seed is skipped — and a body that walks off the Grid genuinely finds nothing under it, which is the
one fall this Grid can produce and is correct. Taking the maximum of the height field and the query
would be two mechanisms answering one question, which is the failure this repository names first.

**A body instance must be asserted rigid at the physics boundary.** A radius pushed through an
instance transform is not frame-invariant the way a ray parameter is: under a scale a transformed
radius is no longer the radius, and under a non-uniform one the sphere is not a sphere. `Instance`
records nothing about whether its transform is rigid, and the failure mode is silent — the picture
stays perfect while contact goes quietly wrong.

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
Grid state and the same camera produce a bit-identical image **on the same device** — see PROGRAM_INTERFACE.md § Determinism
and Replay for what changes across GPUs, which is measured rather than assumed. That is what makes a denoiser unnecessary and what makes
creature training runs reproducible.

The recursion is written as an explicit iterative loop with a small ray stack — compute shaders have no recursion, and
a bounded stack is cheaper and more predictable than one anyway.

---

## Render Targets: Sensors versus the User's Window

Two distinct classes of render target exist, and they are not the same resource.

| | Creature sensor targets | The User's window |
|---|---|---|
| **Purpose** | The input a creature perceives | Debugging and observation for the User |
| **Resolution** | 64 x 64 to 256 x 256, per eye | Whatever the User's window is sized to |
| **Count** | One per eye, several eyes per creature | Exactly one |
| **Camera** | Rigidly attached to the creature | Free-flight debug camera |
| **Format** | `R16G16B16A16_SFLOAT`, read back or sampled by the Program | HDR, then post-processed and presented |
| **Post-processing** | None — creatures receive linear radiance | Bloom and tonemapping |
| **Present** | Never presented | Presented through the swapchain |

The User's view is not privileged in any way that matters: it runs the same tracer over the same BVH. It is simply
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

- **The User's view compares the state it drew from** — camera position, orientation, field of view, surface size — and
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

### What the rule means now that creatures exist

The rule keeps its text and gains a boundary. Two sentences say the whole of it:

- **No pass runs whose output would be identical to the one it already has.**
- **A rezzed Program is itself a reason to tick.** The Grid is committed to knowing nothing about how
  a Program works inside, so it cannot prove that a motionless creature will stay motionless. A
  creature standing still for a hundred ticks and then moving is the expected case, not an anomaly,
  and a Grid that gated the tick on Grid state would reach its first still tick and die — silently,
  deterministically, and invisibly to every check, because a Program cannot tell it has been frozen.

**Only presentation may be skipped for a User-side reason.** Every phase above it is gated on world
state alone. This is not a nicety: `runRenderLoop` has three early exits — a blank or idle window, a
swapchain rebuild, and an out-of-date acquire that the code's own comment says dragging a window edge
produces *constantly* — and each is correct for a picture and catastrophic for a world. Anything that
must not stop when the User grabs a window edge belongs above all three.

The error asymmetry argued above for state comparison over dirty flags argues harder here. A
redundant frame costs one frame; a window that has stopped updating is visible immediately. A
creature whose senses have stopped tracking reality is worse than either, because nothing in the
system is in a position to notice.

### Three idle figures, not one

The 0.09 s against 7.56 s above is measured on a static Grid with a still camera, and that case
survives Phase 6 unchanged. It does not generalise, and stating the other two beside it is cheaper
than letting one imply the others:

| Regime | GPU work per tick | CPU floor per tick |
|--------|-------------------|--------------------|
| Empty roster, still camera | none | unchanged from today |
| N rezzed, all settled | **exactly zero** — the generation does not move, so no eye redraws, no gather re-solves, nothing presents | N early-outs from the physics step, N support probes, N `program_tick` calls, one `144 * (N + 1)`-byte `memcmp` |
| Anything moving | every pass | no saving is claimed, and none ever was |

The first row must be **re-measured** with physics compiled in and shown unmoved, rather than assumed
to still hold.

### "Settled", precisely

A body is settled only when all four hold, and the definition is written out because a vague one
becomes an epsilon and an epsilon drifts:

1. clamped intent is exactly `0.0f` in all three motion channels;
2. the downward support probe found ground within the contact epsilon;
3. achieved velocity is exactly `0.0f`, made so by the settle transition rather than merely small;
4. all three have held for a fixed number of consecutive ticks.

The physics step is still **called** for a settled body and early-outs inside it. A body that runs no
code cannot notice the floor vanishing beneath it or a neighbour arriving, and its wake conditions —
non-zero intent, the support probe losing ground, another body's inflated bounds overlapping — are
evaluated on that path.

The skip is exact rather than approximate, and for the same reason the acoustic skip is. Zero intent
and zero velocity on support give a zero controller target, an integrated delta of
`0.0f * dt == 0.0f`, and gravity cancelled by a support constraint clamping to the height already
held: the pose that would be computed *equals* the pose that is there, bit for bit. Everything
downstream then follows exactly — unchanged pose gives unchanged instance bytes, an equal `memcmp`,
an unchanged generation, an equal `ViewState`, an unchanged gather key. A debug flag runs the full
step for settled bodies and asserts bit-identity with the skipped one, and that assertion is
deliberately broken once before it is trusted.

The settle transition is a genuine one-time discontinuity and is documented rather than argued away:
at most one rest-speed threshold times one tick of position, and one rest-speed of velocity,
appearing as one bounded transient in `specific_force`. It happens once per settle and it is bounded,
which is the most that can honestly be said for it. The absolute figure is deliberately not quoted
here, because it is the product of two constants neither of which is fixed yet.

A divisor was considered for the acoustic gather — solving only when `(tick % D) == 0` — and
rejected. It is a clock inside a rule that says driven by change and never by a clock; the exact
cache key already provides the skip; and [ACOUSTICS.md](ACOUSTICS.md) § Solve rate rules out one
global rate by name. It buys nothing the key does not.

## The Tick

**Where this section stands now.** The tick it describes was built here and then moved: the
physics phase lives in Master Control (its `physics.rs` is the port, held to the goldens this
repository recorded), the flagship deleted its copy, and a `--program` host today runs the
sense phases against the tick the world tells it and sends the intent up the wire. The argument
below - why physics advances once for every body, why staging waits for every Program - is
unchanged and is what Master Control obeys; read "the Grid" in it as the world, wherever the
world runs. The tick is 32 Hz (`dt = 0.03125`), not the 50 Hz some worked examples assume.

Phase 6 built this tick deviceless-first: phases 2 through 9 and 11 ran in the program mode in the
tabled order, while phases 1 and 10 belong to the windowed mode. They meet across the wire:
[TOPOLOGY.md](TOPOLOGY.md) put the spectator in its own process, and the paragraphs below that
speak of a window beside a live roster describe the single-process general form that blueprint
superseded.

The frame flow below is the picture's. This is the world's, and the two meet only at the last step.
Eleven phases, in this order, and the order is forced at every point where it looks arbitrary:

| # | Phase | Reads | Writes |
|---|-------|-------|--------|
| 1 | Drain events | the render channel | camera, surface size |
| 2 | Integrate physics | poses, intents, the hierarchy | poses, velocities, contacts |
| 3 | Republish poses, bump the generation | poses | the instance array, the generation |
| 4 | Render eyes | the hierarchy | eye targets |
| 5 | Gather ears | the hierarchy, source strengths | impulse responses |
| 6 | Read back eye targets | eye targets | host sample buffers |
| 7 | Fill senses | everything above | `TglSenses` per creature |
| 8 | `program_tick` | `TglSenses` | `TglActions` per creature |
| 9 | Clamp and stage actions | `TglActions`, body limits | next tick's intents |
| 10 | Present, if the picture would differ | `ViewState` | the swapchain |
| 11 | Settle or wait | the roster | — |

**Physics advances once per tick for all bodies, never once per creature.** Advancing it inside the
per-creature loop cannot express creature-creature contact at all, and it makes roster iteration
order physically observable in the world.

**Within step 2 the bodies are advanced serially, in ascending roster index**, because bodies push
each other and a parallel push has no defined order. Constraints inside a substep are projected in a
single fixed order, in place, so the answer depends on the sequence: **a reordered roster is a
different animal**, and the roster order is therefore part of the recorded run configuration rather
than a detail of how the array happens to be built. This does not conflict with the ABI's licence to
tick creatures in parallel — that licence is about step 8.

**Senses describe the post-physics state of the same tick.** The ABI already designs in one tick of
actuation latency between a sense and the action it motivates; sensing before physics would stack a
second one on top of it.

**Pose republication sits between physics and both senses** because both senses traverse the top
level, and because the acoustic gather's cache key *is* the generation. A skip cannot be keyed on a
generation that has not yet been recomputed.

**Change detection is a `memcmp` of the instance-record array against the last published copy**, at
the exact granularity the GPU reads, rather than a dirty flag — the same argument the User's view
already makes one layer up, applied one layer down. A future writer who adds a field to
`InstanceRecord` cannot forget to raise anything.

**Eyes are issued before the gather** so the CPU spends the GPU's trace time on the gather. Both are
pure reads of state frozen at step 3, so the order between them cannot change any result, and that is
what makes the overlap free rather than clever.

**Clamping is a separate phase from calling the Programs, run serially in ascending creature index.**
Step 9 is the only phase where Program output enters world state, and therefore the only phase whose
order could ever be observed. Keeping it serial and index-ordered now is what would make a parallel
step 8 safe later; a queue would make application order equal completion order, and float addition is
not associative.

Senses publish the **achieved** speed the integrator produced, never the commanded value, and
`dt_seconds` is always the constant. Non-finite action values are zeroed and logged before clamping,
and the clamped result becomes the next tick's intent.

### What the integrator may not do

**A body is depenetrated along the contact normal with velocity projection; its height is never
assigned.** Assigning `y` from a height field makes `specific_force` the second difference of a
piecewise-linear function, and the Grid's floor is exactly that — `gridMeshHeight` exists because the
drawn triangle is a ramp where the analytic function is a cliff, on a one-metre cell with terraces of
roughly 0.83 m. At one metre per second and a 50 Hz tick a ramp boundary then produces on the order
of 40 m/s² against the 9.81 the ABI promises a resting creature, and a faster tick makes that worse
rather than better, because the same step change is divided by a smaller `dt` twice. The vestibular
channel is the only sense that tells a creature which way is down on a mirror floor, and this is the
cheapest way to poison it.

### Where the picture and the world touch

`ViewState` gains one `uint64_t generation` field, which is the lever it reserved for itself in
advance, so presentation is gated on world change as well as on camera and surface change. With no
creature rezzed the loop waits on the condition variable indefinitely, exactly as it does today; with
a live roster it waits until the next tick deadline.

**Descriptors are written only at a known safe point — after the fence for that frame in flight has
signalled.** That one invariant replaces all update-after-bind reasoning, is auditable in review, and
is what stops the alternating-frame flip-flop failure. It is the general form of the
device-idle-on-resize rule this backend already follows, and per-creature descriptor writes are the
first thing that will need the general form.

On an empty Grid this loop is bit-identical to today's.

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
       │  Sensor trace pass     │                    │  User view trace pass  │
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
description pushed into them. The acoustic pass sits beside them on the sensor branch, dispatching against those same
buffers — one workgroup per ear rather than one thread per pixel — and feeding a histogram instead of an image.

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

## Acoustic Rays

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

## Program Sensor Interface

The contract shipped and is documented normatively in [PROGRAM_INTERFACE.md](PROGRAM_INTERFACE.md);
the diagram below is the shape it took.

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

The User's window sits entirely outside this interface and is never an input to any Program.

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
| Volumetric fog, skybox | The Grid is infinite black; a missed ray costs nothing |
| Third-party physics or audio libraries | In-house keeps the dependency list short: hearing rides the same BVH as vision, and the shipped ground contact is a closed form (`gridMeshHeight`) |
| Rendergraph, event bus, service locator, resource handles | Not enough passes or entity variety to justify the abstraction. Revisit only with a concrete second use case |
| Component / entity layout | Same reason, and it has already been tried here: `components.hpp` carried a `Transform`/`Bounds`/`Geometry`/`MaterialIndex` layout that nothing ever instantiated, which is why it is not there now. Re-proposing it is re-treading ground already paid for once |
| An `Engine` class owning subsystems | Every candidate has exactly one consumer. The Khronos "Building a Simple Engine" chapter is the useful case study precisely because its prose and its shipped reference implementation disagree: the prose teaches component systems, service locators, an event bus with priorities and a topologically-sorted pass manager; the shipped `Engine` is a concrete non-virtual class holding eight named `unique_ptr`s, wired by straight-line construction order, with input arriving through four `std::function` callbacks and no rendergraph at all. Its own conclusion page then says each layer should solve a problem actually encountered rather than an anticipated one, and that "each abstraction adds cognitive overhead and potential failure points" |
| A `libs/physics` extraction | Moot: the physics followed its owner out to Master Control, in Rust, as the one implementation, and this repository keeps none. The rest of this row is the reasoning that stood while it was here. `src/tests/CMakeLists.txt` already says this in as many words, and the reason usually offered for the extraction — so that the check can run in CI — is simply false: the GPU-free ctest targets already run in CI without it |

One pattern from the same chapter is worth taking rather than refusing, because it is an anti-pattern
this repository currently has: **state indexed by the frame in flight belongs in one array of a
`FrameData` struct, not in several parallel vectors each indexed by the same counter.**
`runRenderLoop` keeps command buffers, image-available semaphores and in-flight fences as three
separate vectors walked by one index. Parallel vectors are the shape that lets one of them fall out
of step, and nothing catches it.

---

## Build System

CMake 3.25+ with Ninja Multi-Config. Seven configure presets — five platform presets, plus two
sanitiser variants of `linux-x11-clang`:

| Preset | OS | Compiler |
|--------|----|----------|
| `windows-msvc` | Windows | MSVC (cl) |
| `windows-clang-cl` | Windows | Clang-CL (MSVC ABI) |
| `windows-mingw` | Windows | MinGW-w64 (GCC) |
| `linux-x11-gcc` | Linux | GCC |
| `linux-x11-clang` | Linux | Clang |
| `linux-x11-clang-asan` | Linux | Clang, ASan+UBSan |
| `linux-x11-clang-tsan` | Linux | Clang, TSan |

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
