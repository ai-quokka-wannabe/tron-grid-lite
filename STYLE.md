# Style Guide

Code style conventions for TronGrid Lite.

---

## General Rules

| Rule | Setting |
|------|---------|
| Indentation | 4 spaces (no tabs) |
| Max line length | 170 characters |
| Charset | UTF-8 |
| Final newline | Always |
| Trailing whitespace | Trim (except Markdown) |

These rules are enforced by `.editorconfig`. Install the EditorConfig plugin for your editor:

- **VS Code:** [EditorConfig for VS Code](https://marketplace.visualstudio.com/items?itemName=EditorConfig.EditorConfig)

---

## Single Source of Truth

Avoid duplicating information across files. Each piece of information should have one canonical location.

| Information | Canonical Source |
|-------------|-----------------|
| Build commands | `README.md` § Building |
| Formatting rules | `.clang-format`, `.editorconfig` |
| Security policy | `SECURITY.md` |
| Roadmap & phases | `TODO.md` § Roadmap |
| Creature sensor resolutions | `docs/PERCEPTION.md` |

**Guidelines:**

- Reference the canonical source instead of duplicating content
- If information must appear in multiple places (e.g., PR template checklists), keep it minimal
- When updating information, update the canonical source first
- Cross-reference using `filename` § Section Name format

---

## C++

### Formatting

Use `.clang-format` (LLVM-based). CI does not currently enforce this, but all code should be formatted before committing.

**IMPORTANT — clang-format scope:** run `clang-format -i` only on `.cpp` and `.hpp` files.
**NEVER** pass `.slang` files to clang-format. Slang uses HLSL semantics
(`[shader("fragment")]` attributes, `: SV_Target` semantics, `[numthreads(...)]`)
that clang-format does not understand and will mangle into invalid syntax.

Recommended commands:

```bash
# Format all changed C++ files (safe — globs exclude .slang)
clang-format -i src/*.cpp src/*.hpp

# Or for a specific set of files
clang-format -i src/main.cpp src/tracer.cpp src/tracer.hpp
```

Slang files follow the conventions in § Slang Shaders by hand (Allman braces for functions, 4-space indent, 170 column limit).

Key settings:

| Setting | Value |
|---------|-------|
| Brace style | Allman for functions/namespaces, attached for classes/structs/enums |
| Indent | 4 spaces |
| Column limit | 170 |
| Pointer alignment | Left (`int* ptr`) |
| Braces on bodies | Always — even single-line `if`/`else`/`for`/`while` (`InsertBraces: true`) |

### Naming Conventions

| Item | Convention | Example |
|------|------------|---------|
| Namespaces | PascalCase | `TronGridLite` |
| Types / Classes / Structs | PascalCase | `SwapchainImage` |
| Functions / Methods | camelCase | `createDevice`, `pollEvent` |
| Constants | SCREAMING_SNAKE_CASE | `MAX_FRAMES_IN_FLIGHT` |
| Variables | snake_case | `frame_index` |
| Member variables | m_snake_case | `m_device_handle` |
| Macros | SCREAMING_SNAKE_CASE | `VK_NO_PROTOTYPES` |

### Language Standard

C++20 (and **NOT** beyond it!).

### Error Handling

One mechanism, and two boundaries it must not cross.

**`throw std::runtime_error`** for unrecoverable failures in normal call flow — a bad command-line
argument, an unreadable SPIR-V module, no suitable memory type, no usable GPU. `main()` wraps the
whole run in one `catch (const std::exception&)` that logs via `LoggingLib::Logger::logFatal()` and
returns `EXIT_FAILURE`, so destructors still run and Vulkan objects are released in order.

Library exceptions are caught where they can be acted on: `vk::OutOfDateKHRError` from `vk::raii`
acquire/present is handled at the call site by recreating the swapchain.

#### Failure must not reach `std::abort`

A constructor that throws is the cleanest failure C++ offers, not the messiest: the object never
existed, and every member already built is destroyed in reverse order by the language itself. "The
object cannot be left half-built" is an argument *for* throwing rather than against it.

What abort costs is everything after the diagnosis. `logFatal` flushes the queue and writes to
`stderr` directly, so the *message* survives either way — but the process does not exit, it
terminates abnormally, and three things follow. A caller reading the exit code cannot tell a refused
GPU from a crash. No destructor runs, so nothing is released in order. And on Windows the CRT may
raise its termination dialog, which is a modal box on somebody's screen and a hung job on a machine
with nobody in front of it — the same failure this repository has already met once, from a debug
assertion.

#### Nothing crosses the Program ABI

An exception must never propagate into or out of a Program. That boundary is `extern "C"` into a
shared library the Grid did not compile, where unwinding is undefined behaviour rather than an error
path — the two toolchains need not agree on what a stack frame is. The ABI header marks every
function pointer `noexcept`, which since C++17 is part of the pointer's *type*, so a C++ Program that
omits it fails to compile rather than being asked politely.

### Type Explicitness

Do not use `auto` — write the explicit type so the reader never has to guess. The exceptions are
lambdas, whose types cannot be spelled, and structured bindings, which require `auto` by grammar.

```cpp
// Correct
vk::raii::Pipeline pipeline = device.createGraphicsPipeline(cache, info);
uint32_t count = static_cast<uint32_t>(items.size());

// Wrong
auto pipeline = device.createGraphicsPipeline(cache, info);
auto count = static_cast<uint32_t>(items.size());

// Exception — lambdas have unspellable types
auto on_resize = [&](const WindowLib::WindowEvent& ev) { ... };

// Exception — structured bindings cannot name their type
const auto [acquire_result, acquired_index] = swapchain.get().acquireNextImage(...);
```

### Attributes

Use `[[nodiscard]]` on all functions that return a value the caller must not silently discard —
getters, factory functions, query functions.

```cpp
[[nodiscard]] const vk::raii::Device& get() const;
[[nodiscard]] uint32_t graphicsFamilyIndex() const;
```

### Operator Precedence

Use explicit parentheses when combining arithmetic, bitwise, or increment/decrement operators
with comparison or logical operators. Do not rely on the reader knowing precedence rules:

```cpp
// Correct — each sub-expression is explicit
if ((++frame_counter) % 60 == 0) { ... }
if ((a & mask) != 0) { ... }
if ((file_size <= 0) || (file_size % sizeof(uint32_t) != 0)) { ... }

// Wrong — relies on implicit precedence
if (++frame_counter % 60 == 0) { ... }
if (a & mask != 0) { ... }
```

In compound conditions, parenthesise each sub-expression so it is visually clear
how operations belong together:

```cpp
// Correct — each sub-expression parenthesised
if ((width == 0) || (height == 0)) { ... }
if ((!indices.isComplete()) || (!hasRequiredExtensions(device))) { ... }

// Wrong — relies on reader knowing precedence
if (width == 0 || height == 0) { ... }
```

Single boolean variables do not need extra parentheses — the intent is already obvious:

```cpp
// Fine — single booleans
if (vertex_overflow || triangle_overflow) { ... }
```

### Constants

Use `constexpr` for compile-time constants. Name them `SCREAMING_SNAKE_CASE`. Do not use
plain `const` or magic numbers where `constexpr` applies.

```cpp
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
constexpr float QUEUE_PRIORITY = 1.0f;
```

### Modern C++20 Idioms

Prefer `std::ranges::` algorithms over `std::` + `.begin()/.end()`:

```cpp
// Correct
std::ranges::find_if(devices, predicate);

// Wrong
std::find_if(devices.begin(), devices.end(), predicate);
```

Prefer `std::string_view` for read-only string parameters and comparisons — avoids
unnecessary heap allocations.

Fill vulkan-hpp structs at the point of construction, with designated initialisers. The build defines
`VULKAN_HPP_NO_STRUCT_CONSTRUCTORS` (see `src/CMakeLists.txt`) precisely so this is possible: the
structs become aggregates, `sType` keeps its default, and a temporary can be passed straight into a call.

```cpp
// Correct — designated initialisers on a temporary
command_buffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

m_set_layout = vk::raii::DescriptorSetLayout{m_device->get(),
    vk::DescriptorSetLayoutCreateInfo{.bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data()}};
```

Where a struct must be built up over several statements — a `pNext` chain, or an array whose count
would otherwise be written by hand — use the setters, which derive the count from the container:

```cpp
// Correct — setters on a named local that is assembled in stages
create_info.setPEnabledExtensionNames(extensions);
validation_features.setEnabledValidationFeatures(ENABLED_VALIDATION_FEATURES);
validation_features.setPNext(&debug_create_info);
```

Never assign `count` and `pFoo` members one at a time after construction — that is the C style the
two forms above exist to avoid.

### Include Order

All `#include` directives are flush — no blank lines between groups. Order:

1. Same-module headers (`"device.hpp"`)
2. Project library headers (`<log/logger.hpp>`, `<window/window.hpp>`)
3. Standard library headers (`<vector>`, `<string>`)

```cpp
#include "device.hpp"
#include "instance.hpp"
#include <log/logger.hpp>
#include <window/window.hpp>
#include <cstdint>
#include <string>
#include <vector>
```

### Comment Alignment

Do not column-align trailing comments. Use a single space before `//` or `//!<`:

```cpp
// Correct
SignalsLib::Signal<LogMessage> m_queue; //!< Thread-safe message queue.
std::mutex m_mutex; //!< Protects the wake-up condition.
std::condition_variable_any m_cv; //!< Wakes the worker when messages arrive.

// Wrong — padded to align
SignalsLib::Signal<LogMessage> m_queue;  //!< Thread-safe message queue.
std::mutex m_mutex;                     //!< Protects the wake-up condition.
std::condition_variable_any m_cv;       //!< Wakes the worker when messages arrive.
```

The same applies to enum values — no extra spaces between the value and its comment.

### Constructor Initialiser Lists

Always break after the colon. Each initialiser gets its own line with 4-space indentation.
A single initialiser is one line; multiple initialisers are one per line:

```cpp
Win32Window::Win32Window(const WindowConfig& config, LoggingLib::Logger& logger) :
    Window(logger)
{
}

Swapchain::Swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height, LoggingLib::Logger& logger) :
    m_logger(&logger),
    m_device(&device),
    m_surface(surface)
{
}
```

### Member Initialisation

Use brace initialisation `{}` for all member default values — not `= value` assignment:

```cpp
// Correct
uint32_t m_width{0};
bool m_tracked{false};
const Device* m_device{nullptr};
vk::raii::Device m_device{nullptr};

// Wrong
uint32_t m_width = 0;
bool m_tracked = false;
```

### Vulkan C++ Bindings

Use **vulkan-hpp** with the `vk::raii` namespace for all Vulkan objects. RAII wrappers own their
handles and destroy them automatically — no manual `vkDestroy*` or `device.destroy*` calls.

```cpp
// Correct — vk::raii owns the handle
vk::raii::Image image = device.createImage(image_info);

// Forbidden — non-RAII type used for ownership
vk::Image image = device.createImage(image_info);
```

Non-RAII types (`vk::Image`, `vk::Device`, etc.) are acceptable only as transient parameters
to API calls that don't transfer ownership.

Non-RAII types are never stored as members.

### Resource Ownership

RAII everywhere. Use `vk::raii` for Vulkan objects, `std::unique_ptr` for single-owner heap
objects, and `std::shared_ptr` / `std::weak_ptr` only for signal ownership (see
`libs/signals/include/signal/signal.hpp`).

### Doxygen Comments

All doxygen comments must be proper sentences — capital letter start, period end.

| Style | Use | Example |
|-------|-----|---------|
| `//!` | Single-line brief | `//! Returns the current extent.` |
| `//!<` | Inline member | `uint32_t m_width; //!< Client-area width in pixels.` |
| `/*! */` | Multi-line block | See below |

Multi-line doxygen blocks use 4-space indented content:

```cpp
/*!
    Records a command buffer that transitions the swapchain image to
    colour attachment, clears it to dark teal, and transitions to
    present layout.
*/
```

Use Qt-style backslash commands (`\param`, `\return`, `\brief`) — not Javadoc `@` prefix:

```cpp
/*!
    Selects the best physical device for rendering.

    \param instance The Vulkan instance to enumerate devices from.
    \param surface The target surface used to check present support.
    \return The selected physical device, or std::nullopt if none is suitable.
*/
```

Licence headers use plain `/* */` (not doxygen) with the full GPL v3 notice:

```cpp
/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/
```

---

## Slang Shaders

Follow the C++ conventions where applicable — Slang is syntactically close to HLSL/C++.

**Use HLSL flavour exclusively** — not GLSL. Slang supports both, but we standardise on HLSL
for consistency:

| HLSL (correct) | GLSL (wrong) |
|----------------|--------------|
| `float2`, `float3`, `float4` | `vec2`, `vec3`, `vec4` |
| `float3x3`, `float4x4` | `mat3`, `mat4` |
| `mul(A, B)` | `A * B` for matrices |
| `ConstantBuffer<T>` | `uniform` block |
| `StructuredBuffer<T>` | `buffer` block |
| `SV_Position`, `SV_Target` | `gl_Position`, layout location |
| `(float3x3)matrix` | constructor cast |

### Formatting

| Setting | Value |
|---------|-------|
| Indent | 4 spaces |
| Brace style | Allman for functions, attached for structs |
| Column limit | 170 |

### Naming Conventions

| Item | Convention | Example |
|------|------------|---------|
| Structs | PascalCase | `VSInput`, `VSOutput` |
| Entry points | camelCase | `vertMain`, `fragMain` |
| Struct members | snake_case | `position`, `colour` |
| Constants | SCREAMING_SNAKE_CASE | `MAX_LIGHT_COUNT` |

### Semantics

Use HLSL-style semantics (`POSITION`, `COLOR0`, `SV_Position`, `SV_Target`) — not
`[[vk::location(N)]]` unless explicit location control is required.

### Entry Points

Mark entry points with `[shader("vertex")]`, `[shader("fragment")]`, etc. Use descriptive
names — not `main`:

```slang
[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    ...
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    ...
}
```

### Licence Header

Same GPL v3 `/* */` block as C++ files.

---

## YAML (GitHub Actions)

### Indentation

**4 spaces** for structure levels — aligned with project-wide convention.

```yaml
jobs:
    build:
        name: Build
        runs-on: ubuntu-latest

        steps:
            - name: Checkout
              uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1

            - name: Build
              run: cmake --workflow --preset=linux-x11-gcc
```

### Action Pinning

Third-party and GitHub-owned actions alike are pinned to a full commit SHA with a trailing
`# vX.Y.Z` comment. Dependabot bumps the SHA and rewrites the comment.

### List Item Indentation

List items use **2-space continuation** from the `-` character (standard YAML behaviour):

```yaml
updates:
    - package-ecosystem: "github-actions"
      directory: "/"
      schedule:
        interval: "weekly"
```

### Multi-line Scripts (`run: |`)

Shell script content inside `run: |` blocks uses **4-space indentation** for shell constructs (if/else, loops):

```yaml
            - name: Example step
              shell: bash
              run: |
                if [[ -n "$VAR" ]]; then
                    echo "Variable is set"
                else
                    echo "Variable is not set"
                fi
```

### Structure

- Blank line between top-level keys (`on`, `env`, `jobs`)
- Blank line between jobs
- Blank line before `steps:` in complex jobs
- Comments on their own line, not inline

---

## JSON

### Indentation

**4 spaces**.

```json
{
    "key": "value",
    "nested": {
        "item": 123
    }
}
```

---

## Markdown

### Headings

Use ATX-style headings with blank lines before and after:

```markdown
## Section Title

Content here.
```

### Lists

Use `-` for unordered lists, `1.` for ordered lists.

### Code Blocks

Always specify the language:

````markdown
```cpp
int main()
{
    return 0;
}
```
````

### Trailing Whitespace

Markdown files are exempt from trailing whitespace trimming (needed for line breaks).

### Linting

Use `markdownlint-cli2` to lint Markdown files. Configuration is in `.markdownlint.json` and `.markdownlint-cli2.jsonc`.

```bash
npx markdownlint-cli2 "**/*.md"
```

---

## Tron Naming

The Grid, the Programs that live on it and the User who watches it are named by a settled
vocabulary. Recorded here are only the rulings that a future reviewer, spell-checker or linter would
otherwise "correct" into something wrong.

**The governing principle, which decides the cases not listed:** Tron words name events on the Grid;
plain words name events in the operating system. `program_rez`, `program_tick` and `program_derez`
happen on the Grid. `library_init` and `library_shutdown` are `LoadLibrary`/`dlopen` and
`FreeLibrary`/`dlclose` — facts about Windows and Linux rather than about the Grid — and keep their
plain names.

**Capitalisation:** *Program* and *User* are capitalised when they are the Tron terms, because they
name kinds of being in this world. They are lowercase when they are ordinary English:

| Text | Reading |
|------|---------|
| `This program is free software` | GPL boilerplate — lowercase, untouchable |
| the graphics programme | British English, a different word |
| a Program is rezzed onto the Grid | The Tron term |
| the user runs `tools/record_flyby.py` | Whoever is at the keyboard |
| the User watches through the window | The Tron term |

Three rulings, so that nothing corrects them back:

1. Tron's *Program* is a proper noun and keeps the American single-m spelling. British *programme*
   is a different word and stays British. Both appear in this repository legitimately, and neither
   is ever rewritten into the other — including by § British Spelling below.
2. The GPL boilerplate *"This program is free software"* is a legal text. It is lowercase and
   untouchable, in every licence header in every file.
3. *rez* and *derez* are never glossed as "resolve" or "resolution", and the canon noun
   "de-resolution" is not used in prose here. "Resolution" is used throughout these documents in the
   pixel sense, and in a ray tracer a reader who half-recognises the word would read "rez" as a
   resolution setting.

---

## British Spelling 🇬🇧

Use British spelling in all documentation, comments, and user-facing strings:

| American | British |
|----------|---------|
| color | colour |
| optimize | optimise |
| behavior | behaviour |
| center | centre |
| license | licence |
| meter | metre |
| synchronize | synchronise |
| initialize | initialise |

Code identifiers may use American spelling where it matches library/API conventions (e.g., Vulkan API names).

The Tron term *Program* is not a spelling to correct — see § Tron Naming.

---

## Code Quality Tooling

### Compiler Warnings

Warnings are errors on all compilers — zero-warning policy:

- **MSVC:** `/W4 /WX`
- **GCC/Clang:** `-Wall -Wextra -Wpedantic -Werror`

### Static Analysis (Clang-Tidy)

Configuration in `.clang-tidy`. Bugprone, analyser and concurrency checks are promoted to errors;
everything else is advisory. To run it over the whole project:

```bash
cmake --preset linux-x11-clang -DTGL_ENABLE_CLANG_TIDY=ON
cmake --build build/linux-x11-clang --config Debug
```

It is **off by default** because it roughly doubles build time, and it is **not yet a CI gate** —
which is worth saying plainly, because this section previously implied it was enforced when nothing
executed it at all. `src/` currently passes with zero errors, so promoting it to a gate is a small
change rather than a cleanup project; it has simply not been done.

`clangd` in VS Code also reads the same configuration, which is where the advisory checks surface
day to day.

Two of the enabled checks are deliberately overridden:

- `bugprone-easily-swappable-parameters` is disabled outright. It is an opinion about API shape
  rather than a bug detector, and it fires on every function here that takes an x and a z.
- `readability-uppercase-literal-suffix` wants `1.0F`. This project writes `1.0f` everywhere, so the
  check is left advisory rather than being allowed to rewrite the entire codebase.

To suppress a specific check on a line:

```cpp
int x = legacy_function(); // NOLINT(bugprone-unused-return-value)
```

Note that for a diagnostic reported on a `catch` clause, `NOLINTNEXTLINE` must sit on its own line
*before* the `catch` keyword — putting it inside the block does not suppress anything.

### Runtime Sanitisers

CMake presets for sanitiser builds (Linux Clang only):

```bash
# AddressSanitizer + UndefinedBehaviorSanitizer
cmake --workflow --preset=linux-x11-clang-asan

# ThreadSanitizer (cannot combine with ASan)
cmake --workflow --preset=linux-x11-clang-tsan
```

### Shader Validation

- `spirv-val` validates compiled SPIR-V at build time (malformed bytecode = build failure)
- `slangc -warnings-as-errors all` treats shader warnings as errors

### Vulkan Validation (Debug Builds)

Enabled automatically in debug builds via `VkValidationFeaturesEXT`:

- **GPU-Assisted Validation** — instruments shaders at runtime
- **Synchronisation Validation** — deep barrier analysis
- **Best Practices** — non-optimal API usage warnings

---

*Last updated: 2026-08-01*
