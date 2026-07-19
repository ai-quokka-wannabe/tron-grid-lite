# TODO

The live source of truth for TronGrid Lite work. New work is added as etapes;
criteria are ticked when satisfied; the Journal records what actually happened.

## Roadmap (phases)

| Phase | Goal                          | Milestone                          | Status |
|-------|-------------------------------|------------------------------------|--------|
| 0     | Prove the toolchain           | Triangle on screen                 | In progress |
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
- [ ] CI green on all matrix jobs after adoption

## Etape 2 — Phase 0: triangle on screen

- [ ] Vulkan instance + debug messenger (vk::raii)
- [ ] Physical device selection (prefer discrete GPU)
- [ ] Logical device + queue creation
- [ ] Window via WindowLib (Win32 / XCB)
- [ ] Swapchain (MAILBOX present mode), dynamic rendering
- [ ] Graphics pipeline: triangle.slang (vertex + fragment)
- [ ] Frame synchronisation (fences + semaphores)
- [ ] Triangle on screen

## Journal

### 2026-07-19

- Genesis: repo initialised from TronGrid's genesis commit shape (19-file scaffold,
  hello world, CI green on first push). Org `ai-quokka-wannabe` created the same
  evening; genesis commit pushed.
- Etape 1: inherited all portable infrastructure and the five internal libraries
  from TronGrid; sources moved to `src/`; identity adapted throughout.
