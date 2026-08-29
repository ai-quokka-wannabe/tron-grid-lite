# Development Environment Setup

How to set up a TronGrid Lite build environment from scratch on Windows or Linux, build it,
test it exactly as CI does, and run it. To run the whole ecosystem - the world, the window, a
host with a Program - see [RUNNING_THE_GRID.md](RUNNING_THE_GRID.md) once this builds.

**The short version**, for someone who has done this before:

```text
git clone --recurse-submodules https://github.com/ai-quokka-wannabe/tron-grid-lite.git
cd tron-grid-lite
cmake --workflow --preset windows-msvc        # or linux-x11-gcc, linux-x11-clang, windows-clang-cl, windows-mingw
build/windows-msvc/src/Release/TronGridLite --debug
```

That needs a C++20 compiler, CMake 3.25+, Ninja, the Vulkan SDK 1.4.357.0 (with `slangc` and
`spirv-val` - install every component), and rustup (Rust 1.95.0 is installed by the pin the
moment cargo first runs). Everything below is the long version, with every version CI pins.

---

## Hardware Expectations

The reference development machine is a laptop with an **NVIDIA GeForce GTX 1650 Ti**. That card
exposes **no Vulkan ray-tracing extensions at all** — no `VK_KHR_acceleration_structure`, no
`VK_KHR_ray_query`, no `VK_KHR_ray_tracing_pipeline` — and the project is built accordingly. All
ray tracing is done by hand in ordinary compute shaders against a BVH the project builds itself
into storage buffers.

The practical consequence for you: **any GPU with a working Vulkan 1.3 driver should run this
project.** The project also requires no mesh shaders, no bindless descriptor indexing, and no
hardware ray tracing, so integrated graphics from the last few years are fair game too.

---

## Prerequisites

| Tool | Version | Where to get it |
|------|---------|-----------------|
| CMake | 3.25 or newer | <https://cmake.org/download/> |
| Ninja | 1.11 or newer | <https://ninja-build.org/> (bundled with Visual Studio and most distributions) |
| Vulkan SDK | **1.4.357.0** - the version CI builds against; newer works, the real constraint is `slangc` and `spirv-val` in its `bin` | <https://vulkan.lunarg.com/sdk/home> |
| Git | any recent | <https://git-scm.com/downloads> |
| rustup | any recent; **Rust 1.95.0** is pinned by `external/link/rust-toolchain.toml` and installed by rustup on first use | <https://rustup.rs/> |
| Python | 3.10 or newer, for the `tools/` scripts and the pinned formatter | <https://www.python.org/downloads/> |
| Node.js | 20 or newer, only for the markdown linter (`npm ci`) | <https://nodejs.org/> |

Rust is there for exactly one thing: cargo builds Link — the wire of the Grid — from the
`external/link` submodule, and the build copies the library beside the executables, which is
where it is required to live. Initialise submodules before the first configure:
`git submodule update --init` (or clone with `--recurse-submodules`). You never run cargo
yourself here; CMake does, and the pinned toolchain arrives on its own.

The build uses the **Ninja Multi-Config** generator, so Ninja is not optional.

### Compilers

A complete C++20 implementation is required. Pick one per platform.

| Platform | Compiler | Minimum | Configure preset | Where it comes from |
|----------|----------|---------|------------------|---------------------|
| Windows | MSVC (Visual Studio 2022 or newer) | 19.30 | `windows-msvc` | The "Desktop development with C++" workload |
| Windows | Clang-CL (LLVM for Windows) | 15 | `windows-clang-cl` | The same workload's "C++ Clang tools for Windows", or <https://releases.llvm.org/> |
| Windows | MinGW-w64 GCC | 12 (13.1 on the reference desk) | `windows-mingw` | Qt's Tools (`C:\Qt\Tools\mingw1310_64`, with Ninja in `C:\Qt\Tools\ninja`) or <https://winlibs.com/>; put its `bin` first on the `PATH` in the shell that configures |
| Linux | GCC | 12 | `linux-x11-gcc` | `build-essential` |
| Linux | Clang | 15 | `linux-x11-clang` | `clang` (the sanitiser presets are Clang's) |

CI builds all five on every pull request, and the three sanitiser and verification legs
besides; a warning is an error everywhere (`/WX`, `-Werror`), so build with the compiler you
will be judged by before opening a pull request.

### What the Vulkan SDK is used for

- **Volk** — dynamic loading of Vulkan entry points. The project never links Vulkan statically and
  always builds with `VK_NO_PROTOTYPES` defined.
- **vulkan-hpp** — the `vk::raii` C++ bindings that own every Vulkan object in the codebase.
- **slangc** — the Slang shader compiler. All shaders are written in Slang, never GLSL or HLSL.
- **Validation layers** — essential during development; the debug build enables them.

Ray-tracing components of the SDK are **not** used. You do not need a ray-tracing capable driver,
and `vulkaninfo` need not list a single `VK_KHR_ray_*` extension.

---

## Windows Setup

### Step 1 — Install Visual Studio

Install [Visual Studio 2022 Community](https://visualstudio.microsoft.com/downloads/) or newer and
select the **"Desktop development with C++"** workload. That workload brings MSVC, the Windows SDK,
CMake and Ninja.

If you prefer VS Code, the
[Build Tools for Visual Studio](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio)
package is enough.

### Step 2 — Install the Vulkan SDK

Download from <https://vulkan.lunarg.com/sdk/home> and install with **all components selected** —
in particular Volk, the Slang compiler, `spirv-val`, `spirv-opt` and the validation layers.

The installer sets `VULKAN_SDK` for you. Open a **new** terminal and check:

```bat
echo %VULKAN_SDK%
```

It should print something like `C:\VulkanSDK\1.4.357.0`.

### Step 3 — Verify the driver

```bat
vulkaninfo --summary
```

Confirm that `apiVersion` is at least 1.3 for the device you intend to use. Nothing else needs
checking.

### Step 4 — Clone and configure

```bat
git clone https://github.com/ai-quokka-wannabe/tron-grid-lite.git
cd tron-grid-lite
git submodule update --init
cmake --preset windows-msvc
```

The submodule line is not optional: configure refuses by name when `external/link` is empty,
because the wire of the Grid is built from it.

Run this from a **Developer Command Prompt** ("x64 Native Tools"), or from any shell where
`cl.exe` is on the path - `vcvars64.bat` under the Visual Studio installation puts it there.
For `windows-mingw`, use an ordinary shell with the MinGW `bin` and Ninja first on the `PATH`
instead; for `windows-clang-cl`, the Developer Command Prompt again.

### Step 5 — Build

```bat
cmake --build build/windows-msvc --config Debug
```

### Step 6 — Test

```bat
ctest --preset windows-msvc-debug
```

### Step 7 — Run

```bat
build\windows-msvc\src\Debug\TronGridLite.exe --debug
```

Use the Release configuration (`--config Release`, `build\windows-msvc\src\Release\`) for
anything you want to watch at speed; Debug carries the validation layers and asserts.

This opens the **debug view** — the User's free-flight camera over the built-in stage, with no
world server and no Program, so the rendering can be checked on its own. The live view is
`TronGridLite [host:port] --window`, which demands a running Master Control and refuses loudly
without one; no User inhabits TronGrid Lite either way, and the camera influences nothing.

---

## Linux Setup (Ubuntu / Debian)

### Step 1 — Install compiler and build tools

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git
```

For Clang instead of GCC:

```bash
sudo apt install -y clang
```

Check that CMake is at least 3.25 with `cmake --version`. If your distribution ships something
older, install a current CMake from <https://cmake.org/download/> or from the Kitware APT
repository.

### Step 2 — Install XCB development headers

Window creation on Linux uses XCB directly. Wayland and macOS are not supported.

```bash
sudo apt install -y libxcb1-dev
```

### Step 3 — Install the Vulkan SDK

Follow the LunarG instructions for your distribution at <https://vulkan.lunarg.com/sdk/home>. On
Ubuntu the packaged route is:

```bash
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-noble.list https://packages.lunarg.com/vulkan/lunarg-vulkan-noble.list
sudo apt update
sudo apt install -y vulkan-sdk
```

Replace `noble` with your release code name. Then verify:

```bash
echo $VULKAN_SDK
vulkaninfo --summary
```

If `VULKAN_SDK` is empty, source the SDK's environment script — the tarball installation ships
`setup-env.sh` in the SDK root, and the package installation puts one under `/usr/share/vulkan`.

### Step 4 — Clone, build, test, run

```bash
git clone https://github.com/ai-quokka-wannabe/tron-grid-lite.git
cd tron-grid-lite
git submodule update --init
cmake --preset linux-x11-gcc
cmake --build build/linux-x11-gcc --config Debug
ctest --preset linux-x11-gcc-debug
./build/linux-x11-gcc/src/Debug/TronGridLite --debug
```

---

## CMake Presets Reference

Every build goes through a preset. The repository defines the following.

### Configure presets

| Preset | Platform | Compiler |
|--------|----------|----------|
| `windows-msvc` | Windows | MSVC |
| `windows-clang-cl` | Windows | Clang-CL (MSVC ABI) |
| `windows-mingw` | Windows | MinGW-w64 GCC |
| `linux-x11-gcc` | Linux | GCC |
| `linux-x11-clang` | Linux | Clang |
| `linux-x11-clang-asan` | Linux | Clang + AddressSanitizer + UndefinedBehaviorSanitizer |
| `linux-x11-clang-tsan` | Linux | Clang + ThreadSanitizer |

All of them use the Ninja Multi-Config generator and configure both `Debug` and `Release` in one
build tree, so `--config` selects the configuration at build and test time.

### Build and test presets

Each configure preset has matching build and test presets suffixed `-debug` and `-release` — for
example `windows-msvc-debug`, `linux-x11-gcc-release`. The two sanitiser presets are Debug only.

```bash
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

### Workflow presets

A workflow preset runs configure, build and test in one command:

```bash
# Configure, build Debug and Release, test both
cmake --workflow --preset=windows-msvc
cmake --workflow --preset=linux-x11-gcc

# Sanitiser workflows (Debug only)
cmake --workflow --preset=linux-x11-clang-asan
cmake --workflow --preset=linux-x11-clang-tsan
```

Run the sanitiser workflows before opening a pull request that touches threading or resource
lifetimes.

---

## What CI Runs, and How to Run It at Home

Every pull request runs, on GitHub-owned runners:

| Leg | What it does | At home |
|-----|--------------|---------|
| Quick checks | markdownlint, the pinned clang-format gate, the ABI fingerprint check, the link checker | `npm ci && npm run lint:md`; the formatter below; `python tools/check_abi_version.py check` |
| Build (five compilers) | Debug and Release, every ctest | `cmake --workflow --preset <preset>` |
| Sanitisers | `linux-x11-clang-asan`, `linux-x11-clang-tsan` (Debug) | `cmake --workflow --preset linux-x11-clang-asan` |
| Verification | `--verify-acoustics`, `--verify-scene`, `--verify-senses` against lavapipe, Mesa's software Vulkan | `sudo apt install mesa-vulkan-drivers`, then run the three modes on the Release binary |
| CodeQL | C++, Python and workflow analysis | Read the alert; close it by code, by containment, never by dismissal |

The workflows are in `.github/workflows/`; `ci_pr.yml` is the one to read when a leg is red.

## The Programs Directory

The Grid loads creatures from `programs/` beside its executable - the build creates the
directory and puts the test fixtures there. To host [rc-worm](https://github.com/ai-quokka-wannabe/rc-worm),
build it with a matching compiler, `cmake --install` it, and copy its deployed `programs/*`
(the library, and on Windows the Qt beside it) into this one. `TronGridLite --list-programs`
says what would load and why the rest would not. The whole ceremony, with Master Control and a
window, is [RUNNING_THE_GRID.md](RUNNING_THE_GRID.md).

---

## VS Code Setup

VS Code with CMake Tools is the recommended editor on both platforms.

1. Install VS Code from <https://code.visualstudio.com/>.
2. Open the repository folder and accept the recommended extensions, or install them manually:
    - **C/C++ Extension Pack** (`ms-vscode.cpptools-extension-pack`)
    - **CMake Tools** (`ms-vscode.cmake-tools`)
    - **clangd** (`llvm-vs-code-extensions.vscode-clangd`) — inline Clang-Tidy diagnostics
    - **Clang-Format** (`xaver.clang-format`)
    - **Slang** (`shader-slang.slang-language-extension`)
    - **EditorConfig** (`editorconfig.editorconfig`)
3. Press `Ctrl+Shift+P`, choose **CMake: Select Configure Preset**, and pick your platform preset.
4. Build with `F7`, debug with `F5`.

clangd needs `compile_commands.json`, which the configure step writes into the build directory
(`CMAKE_EXPORT_COMPILE_COMMANDS` is on by default). If clangd reports missing headers, configure
once and reload the window.

---

## Formatting Before You Commit

CI gates on **clang-format 23.1**, pinned to that version because formatters change their
minds between majors. Install exactly that one, into your user site so it cannot collide with
an LLVM on the path, and use it:

```text
python -m pip install --user "clang-format~=23.1"
python -m clang_format --version          # 22.1.x
python -m clang_format -i src/foo.cpp src/foo.hpp
```

Run it on every changed `.cpp` and `.hpp` file. The repository's `.clang-format` enforces
Allman braces, 4-space indent and a 170-column limit. An LLVM `clang-format` of another major
will disagree with the gate on some line, so do not use the one on your `PATH` unless it is 22.

**Never** pass `.slang` files to `clang-format` — it mangles Slang attributes and semantics into
invalid syntax. Format shaders by hand according to `STYLE.md`.

Markdown files must pass markdownlint with the repository's `.markdownlint.json`; the linter
is pinned in `package.json`, so install and run exactly it:

```text
npm ci
npm run lint:md
```

Links are checked weekly by lychee (`.github/workflows/links.yml`); a broken link is a red
run, so check any you add.

---

## Troubleshooting

### CMake reports `Could NOT find Vulkan`

The SDK is either not installed or `VULKAN_SDK` is not visible to CMake.

- Confirm the variable is set: `echo %VULKAN_SDK%` on Windows, `echo $VULKAN_SDK` on Linux.
- On Windows, open a **new** terminal — the installer sets the variable for processes started
  afterwards, not for ones already running.
- On Linux, source the SDK's `setup-env.sh`, or export the variable in your shell profile.
- Delete the stale build directory and reconfigure; CMake caches the failure.

### `VULKAN_SDK` is set but CMake still cannot find it

Check that the path actually exists and points at the SDK root (the directory containing `Bin`,
`Include` and `Lib` on Windows, or `x86_64/bin` and friends on Linux). A version upgrade that
removed the old directory leaves the variable pointing at nothing.

### `slangc` not found

The Slang compiler ships inside the Vulkan SDK, but only if you installed all components. Confirm
that `%VULKAN_SDK%\Bin\slangc.exe` (Windows) or `$VULKAN_SDK/bin/slangc` (Linux) exists. If it does
not, re-run the SDK installer and tick every component.

### Validation layers missing at runtime

Symptom: the debug build logs that `VK_LAYER_KHRONOS_validation` could not be enabled.

- Windows: reinstall the SDK with the validation layer component selected.
- Linux: install the layers explicitly with `sudo apt install -y vulkan-validationlayers`, and make
  sure `VK_LAYER_PATH` either points at the SDK's layer manifests or is unset so the loader can use
  the system ones.
- Verify with `vulkaninfo | grep -i validation`.

### `xcb/xcb.h: No such file or directory`

Install the XCB development headers:

```bash
sudo apt install -y libxcb1-dev
```

### Ninja not found

`cmake --preset ...` fails with `CMAKE_MAKE_PROGRAM is not set`. Install Ninja and put it on the
path — `sudo apt install -y ninja-build` on Linux, or use a Visual Studio Developer Command Prompt
on Windows, which puts the bundled Ninja on the path for you.

### `Unsupported platform` at configure time

The root `CMakeLists.txt` refuses anything that is not Windows or Linux. macOS, Wayland and mobile
targets are out of scope and will not be added.

### A warning stops the build

The project builds with `/WX` on MSVC and `-Werror` elsewhere. Fix the warning; do not disable the
flag.

### `The Link submodule is empty` at configure time

`external/link` was not initialised. Run `git submodule update --init` and configure again;
the wire of the Grid is built from it and the configure step refuses, by name, without it.

### cargo is not found, or Rust is the wrong version

Install rustup from <https://rustup.rs/> and open a new shell; nothing else. The pinned
toolchain (`rust-toolchain.toml` inside `external/link`) is installed automatically the first
time cargo runs in that directory, and CI refuses any workflow that installs a toolchain of
its own, so there is never a second version to disagree with.

### `No Master Control at 127.0.0.1:30702 - is it running?`

`--window` and `--program` are clients of a world; start `master-control` first (or use
`--debug`, which needs no world). [RUNNING_THE_GRID.md](RUNNING_THE_GRID.md) has the order.

### The application starts and the window is black

Expected, in part: the Grid is infinite black with no skybox and mostly black mirror surfaces.
Only emissive neon geometry produces light. If you see nothing at all, fly the camera until neon
comes into view, and check the log for validation errors.

---

*See `CONTRIBUTING.md` for the pull-request workflow and `STYLE.md` for the complete code style
guide.*
