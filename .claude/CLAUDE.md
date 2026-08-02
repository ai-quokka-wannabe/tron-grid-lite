# TronGrid Lite

Deliberately simple Vulkan renderer for the Grid, a Tron-style world perceived by creatures
through rendered frames.

**Two facts that govern every decision in this repo:**

1. **The Grid is for Programs only, and it is the stage rather than the actor.** There is no
   human player, no controls, no gameplay — the User only watches through a free-flight debug
   camera. The repo's whole job is to render senses and apply actions; Programs load as
   shared-library plugins (DLL/SO) behind a plain C ABI, so the Grid stays agnostic about how
   any Program works inside. **Never** add cognition, learning, behaviour models or Program
   internals here, and never write features, docs or comments that assume a human player.
   Programs belong to other repositories in the `ai-quokka-wannabe` organisation.
2. **This is a fresh, independent project.** It reuses infrastructure and foundation code from
   the author's earlier [TronGrid](https://github.com/MatejGomboc/tron-grid) renderer (same
   author, same GPL v3 licence), but it is not a part, fork or component of any other project.
   Describe the relationship as "reuses code from" and nothing stronger.

## Quick Orientation

```text
tron-grid-lite/
├── .claude/CLAUDE.md    ← you are here
├── .github/             ← CI workflows, Vulkan SDK setup actions, dependabot
├── .clang-format        ← LLVM-based, Allman braces, 4-space indent, 170 col
├── .editorconfig        ← 4-space indent, trim trailing whitespace
├── .gitignore
├── CMakeLists.txt       ← top-level build config (finds Vulkan, sets warnings, adds libs/ and src/)
├── CMakePresets.json    ← 7 configure presets: windows-msvc, windows-clang-cl, windows-mingw, linux-x11-gcc, linux-x11-clang, linux-x11-clang-asan, linux-x11-clang-tsan
├── LICENCE              ← GPL v3
├── README.md            ← public-facing project overview
├── TODO.md              ← roadmap, active etapes and journal
├── docs/                ← VISION, ARCHITECTURE, MATERIALS, ACOUSTICS, PERCEPTION, PROGRAM_INTERFACE, RELATED_WORK, DEV_ENV_SETUP
├── images/              ← the flyby clips the README embeds
├── libs/                ← bvh, logging, math, signals, testing, window — static libraries
├── src/                 ← the renderer: main.cpp, Vulkan setup, tracer, postprocess, Slang shaders, tests/
└── tools/               ← Python scripts outside the build (record_flyby.py)
```

## Rules

- **Language:** C++20. No exceptions.
- **Platforms:** Windows (Win32) and Linux (X11) only. No macOS. No Wayland. No mobile.
- **Spelling:** British English everywhere (colour, optimise, metres, synchronise, etc.). The LICENCE file content is untouchable (legal document).
- **Vocabulary:** Tron terms, one word per concept — the Grid, Program, creature, User, tick, senses, actions.
  Glossary in [README.md](../README.md#a-note-on-the-vocabulary); retired: brain, agent, entity, mind, spectator,
  sensor data, motor commands. Tron words name events on the Grid, plain words name events in the OS — which is why
  `library_init` and `library_shutdown` keep plain names. **Program** and **User** are capitalised only as Tron terms:
  GPL boilerplate ("this program is free software") and British "programme" are both left exactly as they are.
- **Formatting:** Run `.clang-format`. Allman braces for functions/namespaces, 4-space indent, 170 column limit.
- **Vulkan loading:** Volk (dynamic). Always define `VK_NO_PROTOTYPES`. Never link Vulkan statically.
- **Shaders:** Slang (not GLSL/HLSL directly).
- **Build:** CMake 3.25+ with Ninja Multi-Config. Use presets: `cmake --preset <name>`, `cmake --build build/<name> --config Debug|Release`.
- **CI:** GitHub Actions. 5 build jobs (one per platform preset) plus 2 sanitiser jobs (ASan+UBSan and TSan, both variants of `linux-x11-clang`).
- **Licence:** GPL v3-or-later.
- **Don't over-engineer.** Keep it simple. No abstractions until there's a concrete second use case. This rule is the entire reason this repo exists.
- **Stay lite.** If a feature needs RT hardware, mesh shaders, or a texture pipeline, it belongs in big TronGrid, not here.

## Building

```bash
# Windows (from VS Developer Command Prompt or with MSVC in PATH)
cmake --preset windows-msvc
cmake --build build/windows-msvc --config Debug

# Linux
sudo apt-get install libxcb1-dev
cmake --preset linux-x11-gcc
cmake --build build/linux-x11-gcc --config Debug
```

**Build `windows-clang-cl` too before pushing, not just `windows-msvc`.** The two disagree about
which warnings exist, and CI runs both under `/WX` and `-Werror`. Clang alone rejects an unused
namespace-scope constant (`-Wunused-const-variable`); MSVC alone rejects an unused local
(`C4189`). A change that builds clean under one can and does fail four CI jobs under the other,
and building the second preset locally costs about a minute against a round trip through CI.

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
| Materials          | One continuous model: colour, index_of_refraction, emission, transmission | The aesthetic is the algorithm |
| Creature vision    | Tiny sensor renders (≤ 800×600, often far less) | Animal eyes resolve little; cheap rays |
| Acoustics          | Same BVH as visual rays                     | One Grid, two senses                       |
| Shader language    | Slang                                       | Modern, modular, multi-target              |
| Vulkan loader      | Volk                                        | Dynamic loading, no link dependency        |

## Roadmap

Phases 0 to 4 are **Done** — toolchain, window and frame loop, compute BVH tracer, full ray tree,
and post processing, and **Phase 5 — acoustic rays** is done too. Phase 6 opens the sensor interface
Programs plug into.

The phase table is canonical in [`TODO.md` § Roadmap](../TODO.md), which also holds the active
etapes and the journal. Read it there rather than duplicating it here.
