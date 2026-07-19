# TronGrid Lite

Deliberately simple Vulkan renderer for a Tron-style world perceived by AI creatures
through rendered frames. Little sibling of the full
[TronGrid](https://github.com/MatejGomboc/tron_grid) — same world rules, none of the
hardcore graphics (no mesh shaders, no hardware RT, no bindless). The AI brains live
in separate repos in the AiQuokkaWannabe organisation and plug in as players; this
repo never contains brain internals.

## Quick Orientation

```text
tron-grid-lite/
├── .claude/CLAUDE.md    ← you are here
├── .github/             ← CI workflows, Vulkan SDK setup actions, dependabot
├── .clang-format        ← LLVM-based, Allman braces, 4-space indent, 170 col
├── .editorconfig        ← 4-space indent, trim trailing whitespace
├── .gitignore
├── CMakeLists.txt       ← build config (finds Vulkan, sets platform defines)
├── CMakePresets.json    ← 5 presets: windows-msvc, windows-clang-cl, windows-mingw, linux-x11-gcc, linux-x11-clang
├── LICENCE              ← GPL v3
├── README.md            ← public-facing project overview
└── main.cpp             ← entry point (currently hello world placeholder)
```

## Rules

- **Language:** C++20. No exceptions.
- **Platforms:** Windows (Win32) and Linux (X11) only. No macOS. No Wayland. No mobile.
- **Spelling:** British English everywhere (colour, optimise, metres, synchronise, etc.). The LICENCE file content is untouchable (legal document).
- **Formatting:** Run `.clang-format`. Allman braces for functions/namespaces, 4-space indent, 170 column limit.
- **Vulkan loading:** Volk (dynamic). Always define `VK_NO_PROTOTYPES`. Never link Vulkan statically.
- **Shaders:** Slang (not GLSL/HLSL directly).
- **Build:** CMake 3.16+ with Ninja Multi-Config. Use presets: `cmake --preset <name>`, `cmake --build build/<name> --config Debug|Release`.
- **CI:** GitHub Actions. 5 matrix jobs matching the 5 presets.
- **Licence:** GPL v3-or-later.
- **Don't over-engineer.** Keep it simple. No abstractions until there's a concrete second use case. This rule is the entire reason this repo exists.
- **Stay lite.** If a feature needs RT hardware, mesh shaders, or a texture pipeline, it belongs in big TronGrid, not here.

## Building

```bash
# Windows (from VS Developer Command Prompt or with MSVC in PATH)
cmake --preset windows-msvc
cmake --build build/windows-msvc --config Debug

# Linux
sudo apt-get install libx11-dev
cmake --preset linux-x11-gcc
cmake --build build/linux-x11-gcc --config Debug
```

## Target Hardware

Any Vulkan 1.3 GPU — **no ray tracing extensions required or used**. Reference dev
machine: GTX 1650 Ti laptop (exposes zero VK RT extensions; that is the point).

## Key Design Decisions

| Decision           | Choice                                      | Why                                        |
|--------------------|---------------------------------------------|--------------------------------------------|
| Coordinate system  | Right-handed, Y-up                          | Matches glTF, most tools                   |
| Units              | Metres                                      | Physically-based lighting                  |
| Colour space       | Linear internal, sRGB output                | Correct blending                           |
| HDR range          | 16-bit float                                | Emissive glow needs headroom               |
| Present mode       | MAILBOX                                     | Low latency, no tearing                    |
| Ray tracing        | Deterministic Whitted in compute shaders    | Perfect mirrors + emissives = no MC noise  |
| Materials          | Mirror / emissive / glass only              | The aesthetic is the algorithm             |
| Creature vision    | Tiny sensor renders (≤ 800×600, often far less) | Animal eyes resolve little; cheap rays |
| Acoustics          | Same BVH as visual rays                     | One world, two senses                      |
| Shader language    | Slang                                       | Modern, modular, multi-target              |
| Vulkan loader      | Volk                                        | Dynamic loading, no link dependency        |

## Roadmap

Currently in **Phase 0 — Foundation** (triangle on screen).

| Phase | Goal                          | Milestone                          |
|-------|-------------------------------|------------------------------------|
| 0     | Prove the toolchain           | Triangle on screen                 |
| 1     | Window, swapchain, frame loop | Fly through a wireframe grid       |
| 2     | BVH + primary rays in compute | Mirror world, first bounce         |
| 3     | Full ray tree                 | Reflections, emissives, glass      |
| 4     | Post processing               | Bloom, tonemapping                 |
| 5     | Acoustic rays                 | Echoes and occlusion via same BVH  |
| 6     | AI players                    | Creature sensor interface plugs in |

## Phase 0 Checklist

- [ ] Integrate Volk for dynamic Vulkan loading
- [ ] Create platform window (Win32 / X11)
- [ ] Vulkan instance + debug messenger
- [ ] Physical device selection (prefer discrete GPU)
- [ ] Logical device + queue creation
- [ ] Swapchain setup (MAILBOX present mode)
- [ ] Render pass + framebuffers
- [ ] Graphics pipeline (vertex + fragment, hardcoded triangle)
- [ ] Command buffer recording + submission
- [ ] Frame synchronisation (fences + semaphores)
- [ ] Triangle on screen
