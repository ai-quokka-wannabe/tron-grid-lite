# TODO

The live source of truth for TronGrid Lite work. New work is added as etapes;
criteria are ticked when satisfied; the Journal records what actually happened.

## Roadmap (phases)

| Phase | Goal                          | Milestone                          | Status |
|-------|-------------------------------|------------------------------------|--------|
| 0     | Prove the toolchain           | Triangle on screen                 | **Done** |
| 1     | Window, swapchain, frame loop | Fly through a wireframe grid       | Pending |
| 2     | BVH + primary rays in compute | Mirror world, first bounce         | Pending |
| 3     | Full ray tree                 | Reflections, emissives, glass      | Pending |
| 4     | Post processing               | Bloom, tonemapping                 | Pending |
| 5     | Acoustic rays                 | Echoes and occlusion via same BVH  | Pending |
| 6     | AI players                    | Creature sensor interface plugs in | Pending |

## Etape 1 — Adopt project infrastructure from TronGrid

- [x] Port linting configs, governance docs, templates, workflows, Claude commands
- [x] Port internal libraries (testing, signals, logging, math, window) with tests
- [x] Move sources to `src/`, add GPL v3 licence headers, include volk.cpp in build
- [x] Adapt all name references to TronGrid Lite / ai-quokka-wannabe
- [x] CI green on all matrix jobs after adoption

## Etape 2 — Phase 0: triangle on screen

- [x] Vulkan instance + debug messenger (vk::raii)
- [x] Physical device selection (prefer discrete GPU)
- [x] Logical device + queue creation
- [x] Window via WindowLib (Win32 / XCB)
- [x] Swapchain (MAILBOX present mode), dynamic rendering
- [x] Graphics pipeline: triangle.slang (vertex + fragment)
- [x] Frame synchronisation (fences + semaphores)
- [x] Triangle on screen

## Etape 3 — Phase 1: window, swapchain and frame loop

- [ ] Spectator camera wired to input (free flight, for the human observer only)
- [ ] Wireframe grid geometry to fly through
- [ ] Depth buffer
- [ ] Frame timing and a simple statistics log

## Etape 4 — Phase 2: the compute ray tracer

- [ ] Triangle and material storage buffers
- [ ] BVH builder on the host, uploaded as a storage buffer
- [ ] Compute traversal kernel: primary rays only, mirror surfaces
- [ ] Write results into the swapchain image (or an offscreen image plus blit when the
      swapchain does not support storage writes)

## Journal

### 2026-07-19

- Genesis: repo initialised from TronGrid's genesis commit shape (19-file scaffold,
  hello world, CI green on first push). Org `ai-quokka-wannabe` created the same
  evening; genesis commit pushed.
- Etape 1: inherited all portable infrastructure and the five internal libraries
  from TronGrid; sources moved to `src/`; identity adapted throughout.
- Etape 1b: inherited the Vulkan foundation as well — instance, device, surface, swapchain,
  VMA allocator, camera, components and scene — each adapted to the lite scope. The device
  requests nothing beyond Vulkan 1.3 core (dynamic rendering, synchronisation2) plus the
  swapchain extension: no ray tracing, no mesh shaders, no bindless. Ported the three shaders
  that survive the scope cut (triangle, postprocess, bloom) and dropped the rest. Documentation
  written fresh for Lite: VISION, ARCHITECTURE, AGENT_INTERFACE, DEV_ENV_SETUP.
- Etape 2 (Phase 0 milestone): **triangle on screen.** Window, instance, device, swapchain,
  dynamic rendering, Slang compilation with SPIR-V validation, and double-buffered frame
  synchronisation all verified end to end on the reference GTX 1650 Ti. Validation layers report
  no errors; they confirm the device requirements are honest by disabling their own ray-query,
  trace-ray and mesh-shading checks because the hardware lacks those features. Five test suites
  pass.
