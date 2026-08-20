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
├── TODO.md              ← roadmap and open etapes
├── docs/                ← VISION, ARCHITECTURE, MATERIALS, ACOUSTICS, PERCEPTION, PROGRAM_INTERFACE, TOPOLOGY, RELATED_WORK, DEV_ENV_SETUP
├── external/link        ← the Link submodule — the wire of the Grid, built by cargo, loaded beside the executable like a Program
├── images/              ← the flyby clips the README embeds
├── libs/                ← bvh, logging, math, signals, testing, window — static libraries — and program-abi, the C contract Programs build against
├── src/                 ← the renderer and the world: main.cpp, Vulkan setup, tracer, postprocess, Slang shaders, the roster, stage and acoustics, tests/
└── tools/               ← Python scripts outside the build (record_flyby.py, check_abi_version.py)
```

## Rules

- **Language:** C++20. Exceptions are used, narrowly, and three rules bound them. **They are thrown
  only for unrecoverable failure in normal call flow** — `throw std::runtime_error`, plus whatever
  `vk::raii` throws on our behalf — and caught once in `main`, which logs fatal and returns
  `EXIT_FAILURE`. See [STYLE.md](../STYLE.md) § Error Handling. **They must never cross the Program
  ABI**: that boundary is `extern "C"` into a library the Grid did not compile, where an unwind is
  undefined behaviour rather than an error path. The Program is bound to the same prohibition from
  its own side by [PROGRAM_INTERFACE.md](../docs/PROGRAM_INTERFACE.md) § Rules, and the rule is
  written twice because each side is read alone. And **failure must not reach `std::abort`** — a
  command-line program owes its caller a meaningful exit code, and abort delivers an abnormal
  termination instead, runs no destructor, and on Windows can raise the CRT's termination dialog,
  which on a machine with nobody watching is a hang rather than a failure. `logFatal` does flush and
  write to stderr before it, so the diagnosis survives; what does not survive is the difference
  between "this program failed" and "this program crashed".
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
- **CI:** GitHub Actions. 5 build jobs (one per platform preset), 2 sanitiser jobs (ASan+UBSan and TSan, both variants of `linux-x11-clang`),
  and a software-Vulkan job running the three `--verify` modes against lavapipe.
- **Licence:** GPL v3-or-later.
- **Don't over-engineer.** Keep it simple. No abstractions until there's a concrete second use case. This rule is the entire reason this repo exists.
- **Stay lite.** If a feature needs RT hardware, mesh shaders, or a texture pipeline, it belongs in big TronGrid, not here.

## Building

Initialise submodules first (`git submodule update --init`) — configure refuses without the Link
submodule, and cargo (rustup stable) must be on PATH because Link's own CMake face demands it;
this repository's build files never name it. **Link's
library lives beside the executable, always** — no path flag, no search order (the owner's rule);
`LinkLib::Library::besideExecutable()` is the one resolver and `lnk_copy_beside` is what makes it true.

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

**`windows-mingw` builds on this machine too, and it is the one worth the extra minute.** GCC's
warning set is the one neither MSVC nor clang-cl approximates, and it is the preset that has actually
caught things: a Program library named `libfoo.dll` instead of `foo.dll`, found by CI after
everything local was green. The toolchain came from the Qt Maintenance Tool as **MinGW 13.1.0
64-bit**; Ninja is already on `PATH` and nothing else from that installer is needed.

Prepend it for the build only rather than putting it on the system `PATH`, so it cannot shadow
anything, and do not run it from the VS developer shell:

```powershell
$env:PATH = 'C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
cmake --preset windows-mingw
cmake --build build/windows-mingw --config Debug
ctest --test-dir build/windows-mingw --build-config Debug
```

**The two Linux presets are CI-only, and the reason is one dependency rather than the platform.**
WSL runs Ubuntu 24.04 here with gcc 13.3, cmake, ninja and `libxcb1-dev` already present; what is
missing is the Vulkan SDK, because `slangc` and `spirv-val` are demanded at configure time and
neither ships with the distribution. Installing it would complete the set of five locally, which
is worth knowing whenever CI is unreliable.

Until then, a change that is pure portable C or C++ with no `#ifdef` in it is covered by MinGW's
GCC, which is the same compiler family and the same warning set the Linux jobs use.

## Target Hardware

Any Vulkan 1.3 GPU — **no ray tracing extensions required or used**.

**Two machines, four devices, three vendors.** The daily machine is the first pair; the second is
still available and is exercised about weekly, which is what keeps the modest-hardware claim honest
rather than remembered.

| Machine | # | Device | Type | Vulkan | Cadence |
|---------|---|--------|------|--------|---------|
| daily | 0 | Intel RaptorLake-S Mobile Graphics | integrated | 1.4.323 | every change — the device under test |
| daily | 1 | NVIDIA GeForce RTX 4090 Laptop | discrete | 1.4.341 | every change — speed, and where an RT dependency would hide |
| weekly | 0 | AMD Radeon(TM) Graphics | integrated | 1.3.260 | about weekly — the third vendor |
| weekly | 1 | NVIDIA GeForce GTX 1650 Ti | discrete | 1.4.341 | about weekly — discrete, no RT, older generation |

Three vendors is the part worth having. NVIDIA and AMD share more lineage than either shares with
Intel, so an Intel integrated part is the least like anything this renderer was developed against —
and it is the cheapest of the four to run, being in the daily machine.

**The Intel part is the device under test, and paradoxically that is because it is the weakest
thing here.** It exposes no ray-tracing extension at all, it is integrated, and it is the slowest of
the four — so it guards both claims this project makes about hardware, and it does so on the daily
machine rather than weekly. Measured rather than assumed:

| Device | Ray-tracing extensions exposed |
|--------|-------------------------------|
| Intel RaptorLake-S Mobile Graphics | none |
| NVIDIA GeForce RTX 4090 Laptop | `acceleration_structure`, `ray_query`, `ray_tracing_pipeline`, `ray_tracing_position_fetch`, `NV_ray_tracing` |
| NVIDIA GeForce GTX 1650 Ti | none |

**Having one device that does expose them is worth as much as the ones that do not.** An accidental
dependency on a ray-tracing extension would run perfectly on the 4090 and fail on everything else, so
the 4090 is where such a mistake would hide and the Intel part is where it would surface. Run the
Intel one when the question is "does this still work at all"; run the 4090 when the question is "how
fast", and never the other way round.

To re-take that inventory on new hardware, note that `vulkaninfo` emits one block per device headed
`GPUn:` and a naive grep attributes extensions to whichever device it happens to reach first — which
produced a confidently backwards answer here before the blocks were split properly:

```bash
"$VULKAN_SDK/Bin/vulkaninfoSDK.exe" > info.txt   # then split on /^GPU[0-9]+:$/ before searching
```

Device scoring always picks the discrete one, so
`--gpu <index>` exists to force the other, and `--list-gpus` reports every device with the reason
any unusable one is unusable. **Verify on both before claiming anything about correctness**: this
repository has already shipped one piece of reasoning whose only evidence was that it worked on the
driver in front of us, and cross-vendor testing is what turns that into evidence.

Determinism is bounded by the device: bit-identical on one GPU, *close* across two. Cross-device
bit-identity is not a goal because it is not reachable — IEEE-754 pins the four operations and
`sqrt`, but not fused-multiply-add contraction and not the transcendentals. The target is small and
measured. See [PROGRAM_INTERFACE.md](../docs/PROGRAM_INTERFACE.md) § Determinism and Replay.

## The reference render

The check that has **licensed** more structural rewrites here than any other: record a fixed clip and
hash it. A change that is meant to preserve behaviour must not move a single byte.

"Licensed" rather than "caught", deliberately. Fifteen commits cite byte-identity — recount with
`git log --oneline --grep=byte-ident` — and every one of them
is a rewrite *confirmed safe* — the traversal module extraction, the geometry hoist into `World`, the
memory arena, both halves of the two-level hierarchy. **Not one records a catch.** That may only mean
catches get fixed before they reach a commit message, but by this file's own standard a check is worth
what it has demonstrably done, and what this one has demonstrably done is let the innermost loop of
every ray be replaced on evidence rather than on argument. That is worth a great deal here and is not
the same product as finding bugs.

```bash
build/windows-msvc/src/Release/TronGridLite.exe --record --frames 12 --width 640 --height 360 --output <dir>
# sha256 of frame_00000.ppm .. frame_00011.ppm concatenated in name order
```

| Vendor | Devices measured | Digest |
|--------|------------------|--------|
| NVIDIA | GeForce RTX 4090 Laptop (Ada Lovelace) | `70DDA0E77A7B9A0560F4DE4AC0FA6268A39C1B60CF3F6AA8F1E84D4E72A0593F` |
| AMD | pending: the weekly machine has not seen the vertical risers yet | — |
| Intel | RaptorLake-S Mobile Graphics, integrated | `BEB778C562B09B955AE92BEB8472E800F965E8E585B24CB3390E7F8CD8EFBB98` |

The digests moved legitimately with the vertical-riser floor — the picture was meant to change —
and were re-recorded on the daily machine's two devices in the same commit. Two rows wait for the
weekly machine: the AMD digest, and the NVIDIA row's second card (the GTX 1650 Ti), whose agreement
with the 4090 is what backs the per-vendor claim below. Until it is re-measured, that claim rests
on the previous scene's evidence.

**Determinism is per vendor rather than per device, and that is measured rather than assumed.** Two
NVIDIA cards two architecture generations apart — Turing and Ada Lovelace, different machines,
different drivers — produce output that is **identical to the last byte**. This file previously said
determinism was per device, which was the cautious reading of a sample of one card per vendor, and it
was wrong in a way that mattered: it implied a recorded digest was worthless to anybody else, when in
fact any NVIDIA machine can check against the one above.

What varies is the vendor's shader compiler, which is where the freedom IEEE-754 leaves — contraction
of multiply-adds and the transcendentals — actually gets spent.

| Comparison | Bytes differing | Worst delta |
|------------|-----------------|-------------|
| NVIDIA vs AMD | pending the weekly machine (16.38% and 224 of 255 on the previous scene) | — |
| NVIDIA vs Intel | 2.95% | 197 of 255 |

The NVIDIA-to-Intel figure barely moved across a scene change that rebuilt the whole floor — 2.97%
became 2.95% — which is exactly the stability these rows are kept to watch.

Alarming figures that are not: this is a world of mirrors, so a ray passing one side of an edge
instead of the other lands on a neon tube instead of on black, and a single contracted multiply-add
decides which. They are worth keeping precisely because they are stable — measure before and after a
change, and if a *cross-vendor* figure moves, something has become genuinely less deterministic.

**Do not read the NVIDIA result as a promise.** It is two cards, one vendor, and a driver pairing
nobody controls. A third NVIDIA card disagreeing would be a fact about that driver rather than a
regression, and the way to tell is to re-run an unchanged binary before believing anything.

**What the digest cannot see.** It is a check on the picture, and the picture is produced by a
single-threaded path. A lock inversion, a lost wakeup, or a `join()` waiting behind
`acquireNextImage(UINT64_MAX)` all leave it green. So does ThreadSanitizer, which proves the absence
of *races* — a safety property — and says nothing about order. Anything that touches threading needs
its own check, and a refactor of the threading is precisely the case where a green digest is least
reassuring and feels most so.

**A mismatch has two suspects, not one.** The digest is a check on the code and, unintentionally, a
check on the machine: re-running an unchanged binary and getting a different digest is
near-conclusive evidence of hardware rather than of anything in the tree. The reverse follows and is
the awkward half — a mismatch after an edit is ambiguous between the edit and a flipped bit, which
is the one thing the check exists to disambiguate. Re-run the unchanged binary before believing
either verdict, and treat that as mandatory rather than cautious on any machine with a history of
unexplained crashes.

**Moving code between targets cannot move the digest by contraction.** No preset and no
`CMakeLists.txt` sets `INTERPROCEDURAL_OPTIMIZATION`, `-flto` or `/GL`, so a translation unit's
floating-point behaviour does not depend on which static library it lands in. Re-take the digest
after a structural move anyway — it is cheap against a lost afternoon — but do not budget GPU rounds
for that specific fear.

A digest changes legitimately whenever the picture is meant to change. Update the table in the same
commit, and say in the message what moved and why.

## The shape of the program

**Command-line first, and a client of Master Control from the very first run.** The one positional
argument is where the world is — `host:port`, defaulting to `127.0.0.1:30702` — and the flags say
what you are there; the full ruling lives in [TOPOLOGY.md](../docs/TOPOLOGY.md) § The shape. A
creature perceives through a senses buffer, never a swapchain, so a run that hosts Programs needs no
display and `Device` accepts a null surface — no present queue is sought and `VK_KHR_swapchain` is
neither required nor enabled. A compute-only card with no monitor is a perfectly good device here.

**`--window` is the live view, and it refuses loudly when nobody is listening** — "No Master
Control at `<addr>` - is it running?" — because a fallen server must never look like an empty
world; there is deliberately no silent fallback. It draws what TICK_STATE tells, interpolated
between the two newest ticks (`src/world_client.hpp`), with no prediction ever. **With a window
there are no locally hosted creatures** — a window is for the User's eyes; a Program that wants to
show its own internals opens its own window, and the Grid provides it nothing.

**`--debug` is the standalone stage view**: the same window on the built-in stage, no server and no
Program, for checking the rendering where no world is running. It is also where the on-demand gate
— skipping whole frames while the User drags a window edge — keeps its licence: nothing there is
alive, so no tick can be missed. With a live world that licence expires, and the loop turns at a
steady pace instead, draining the wire whole each turn.

## The three checks worth running

All of them need a device but none needs a display, and a device does not have to be a GPU. **The
verification modes run in CI on every push**, against lavapipe — Mesa's software Vulkan driver, which
satisfies everything this renderer asks for (Vulkan 1.3, dynamic rendering, synchronisation2, a
graphics family that dispatches compute) and answers each comparison in about a second.

That leaves `--benchmark` as the one still needing hardware, which is right: it measures a GPU, and a
software implementation has nothing to say about that.

Running them locally on real hardware is still worth doing, and for a reason CI cannot cover. Lavapipe
is one implementation; the value of the checks is that they hold across implementations that share
nothing, and the two GPUs in this machine differ from each other and from a CPU in different ways.

To reach lavapipe locally, point the loader at its ICD and nothing else — `VK_ICD_FILENAMES` replaces
the driver list rather than adding to it, which is what makes it a clean third opinion:

```bash
VK_ICD_FILENAMES=<path>/lvp_icd.x86_64.json  TronGridLite --verify-scene
```

On Windows the driver comes from `pal1000/mesa-dist-win` (`vulkan_lvp.dll` plus its ICD JSON); on
Debian or Ubuntu it is `mesa-vulkan-drivers`. The Vulkan SDK ships neither — it has headers only.

| Command | Answers |
|---------|---------|
| `--verify-acoustics` | Does `acoustics.slang` compute what `Acoustics::gather` computes? |
| `--verify-scene` | The same question with the Grid placed **at an angle**, which is the only way any transform arithmetic gets exercised on the device. At the identity a matrix and its transpose are the same sixteen numbers. |
| `--verify-senses` | Does `senses.slang` answer a sample ray with what the host's first hit says? At one bounce the Whitted walk collapses to a copied emission constant, so the agreement is exact — which checks the senses pass's own plumbing, while the shading it shares with the renderer stays licensed by the digest. |
| `--benchmark` | What each GPU pass costs, from the device's own timestamps. Run-to-run spread about 2%, against 10% for timing `--record` with a wall clock. |

`--benchmark` writes nothing and reads nothing back, discards ten warm-up frames, and walks the same
fixed camera path as `--record` so that two runs are comparable. **Never measure a pass by timing
`--record`** — most of that wall clock is PPM files.

**One check does not belong in this table, and that is the point of mentioning it.** "Fires only
when somebody remembers it" is a property of *these three* rather than of checking in this
repository: the per-tick physics state hash needs no device, so it runs under `ctest` on every push
— the first determinism check here that fires without being remembered. It lives in
`src/tests/roster_tests.cpp`, was broken deliberately once with an address-derived perturbation,
which it caught, and asserts per build rather than against golden values, because yaw goes through
`sin` and `cos` and no two libms agree in the last bit.

## Hard-won rules

Each of these cost real time at least once. They are here rather than in a journal because a lesson
written as a story is read once, and written as a rule it is read every session.

**Verifying**

- **Confirm the build succeeded before believing a test result.** A failed build leaves the previous
  binary in place, and `ctest` will cheerfully report that it passes. This invalidated two
  experiments before anyone noticed.
- **Measure in Release.** Debug enables GPU-assisted validation, which instruments every buffer
  access in the traversal loop — the trace pass reads about 4.6x slow, and post-processing barely
  moves, so the distortion is per-pass and cannot be corrected by a single factor. Every performance
  figure in this repository was once wrong for this reason.
- **A test that has never failed has not been tested.** Break the thing it guards, watch it go red,
  put it back. Two tests in this repository passed while asserting something a bug could not violate.
- **Check that the comparison had something to compare.** Two implementations agreeing on *nothing
  arriving* agree perfectly. `--verify-scene` reported host and device matching to the last digit on
  its first run, because the listener had been left behind when the world moved; only the "did
  anything arrive at all" floor noticed. Every comparison here carries such a floor, and each one was
  added after it caught something.
- **Distrust a measurement with no mechanism, whichever way it points.** A 7% speedup from adding a
  level of indirection was warm-up. The same measurement 7% the other way would have looked exactly
  like the honest cost of the feature, and would have been believed.
- **Confirm an optimisation exists before costing its loss.** Two separate design proposals argued
  against a change on the grounds that it would kill the acoustic solve cache — at a time when there
  was no cache: `src/acoustics.hpp` stated, in the future tense, an obligation on whatever would
  eventually drive solves, and nothing drove them yet. The hum cache has since been built
  (`src/senses.hpp`), which retires the example without retiring the rule: an argument whose weight
  rests on a named mechanism should name the file the mechanism lives in, because a specification
  reads exactly like an implementation once it has been quoted twice.
- **Verify on both GPUs before claiming correctness** — see Target Hardware. Driver-specific
  behaviour is invisible on one.
- **Grep the ancestor before planning to port anything out of it.** The earlier TronGrid describes a
  windowed-versus-command-line duality in its README, its vision document and its AI interface
  document, and implements it nowhere: `main` takes no `argv`, there is no mode flag, and the
  window is constructed before the Vulkan instance exists, so a device that cannot present is
  refused with `std::abort`. The remembered capability was prose, and the code did the opposite of
  it. Two greps — one for the flag, one for the mechanism — would have replaced an afternoon of
  planning to port something that was never written.

- **A red CI check is not necessarily a failure, and the difference is in the job rather than the
  log.** `gh pr checks` prints `fail` for a job that was *cancelled*, which is what a job looks like
  when it never got a runner at all — and the tell is that it records **zero steps** and dies at a
  round number of minutes. The other shape is a job that starts and then cannot fetch what it needs,
  which surfaces only as an annotation: `Failed to resolve action download info. Service
  Unavailable`. Neither is anything in the tree, and an afternoon went into treating one as though it
  were. Ask for the job's conclusion and its step count before reading a single line of log:

  ```bash
  gh api "repos/<owner>/<repo>/commits/$(git rev-parse HEAD)/check-runs" \
      --jq '.check_runs[] | "\(.name): \(.status)/\(.conclusion) \(.started_at)"'
  gh api "repos/<owner>/<repo>/check-runs/<id>/annotations" --jq '.[].message'
  ```

  A CodeQL run from GitHub's default setup **cannot be retried** through the API, so when one of those
  is the casualty the only way to get a fresh verdict is a new commit on the branch.

**Where the bugs actually are**

Three questions, each of which has caught something more than once:

1. **Where does this cross a boundary the compiler cannot see?** Host constant against shader
   literal, host memory against device memory, a documented ABI layout against the code implementing
   it. Nearly every serious defect found here has been a duplicated fact with nothing holding the
   copies together — and **a comment saying "must equal" is not a mechanism.** Use a `static_assert`
   against the other side's literal.
2. **Where does a document state a number that could be checked?** Deposit counts, warning counts,
   allocation counts, phase status. They drift silently and nothing fails.
3. **Where does this repository already do the same thing correctly?** The cinematic readback had a
   host-visibility barrier the acoustic pass lacked; `BAND_COUNT` had an assert `MAX_DEPTH` lacked.
   The correct twin is usually a few files away.

A fourth question is worth asking of any *restructuring*, and it is the one that stops work rather
than finding a bug:

**What defect does this restructuring fix?** If the answer is "the file is long", there is no
defect. Moving named, greppable, namespace-scope functions out of a large file gains a reader
nothing that `grep -n "^[A-Za-z].*("` does not, and costs a diff of over a thousand lines that the
reference render must then license. The restructurings worth doing are the ones that make a mistake
unrepresentable — a type that owns two objects which must be resized together, a `static_assert`
against the other side's literal — and each of those can name the mistake it prevents.

**Windows answers some failures with a dialog box rather than an error code**

- **`LoadLibrary` on a malformed library raises a modal "Bad Image" box and does not return until
  somebody dismisses it.** On a build machine or an unattended run that is a hung process rather than
  a failed one — the same trade this repository already refuses around `std::abort` and the CRT's
  termination dialog. `SetThreadErrorMode(SEM_FAILCRITICALERRORS, …)` around the call suppresses it;
  per-thread rather than `SetErrorMode`, which is process-wide and not the loader's to change.
  Capture `GetLastError` before restoring the mode, since it is what the refusal quotes.
- **A child process inherits the process error mode, so "change nothing" is not a control.** A probe
  written to reproduce this could not, because it was launched from a shell that already had
  `SEM_FAILCRITICALERRORS` set and inherited it — and `GetThreadErrorMode` returns 0 in that state,
  which reads exactly like "no suppression". Clear it with `SetErrorMode(0)` explicitly to get a real
  control, and expect the unsuppressed run to hang rather than fail.

**Tooling on this machine**

- **Never edit a file through PowerShell `Set-Content` or `Out-File`.** The round trip re-encodes
  UTF-8 as ANSI and turns every em dash into mojibake, and it adds a BOM. Use the editing tools or
  Python with an explicit encoding.
- **`Copy-Item` and `Move-Item` preserve the source timestamp**, so Ninja concludes there is nothing
  to rebuild and the next test runs the old binary. Touch the file after restoring one.
- **Exclude `build/` from repository-wide greps.** Searching for a removed symbol otherwise returns
  a wall of `.pdb` matches and buries the real hits.
- **Backslash escapes do not survive a shell heredoc into Python.** `b"\\return"` arrives as
  `b"\return"`, which is a carriage return followed by `eturn` — so a Doxygen tag written that way
  lands in the source as a control character and a misspelt word, invisible in every editor. Three
  reached `main` this way, and the first attempt to *repair* them added four more, because the
  search pattern was mangled identically to the replacement and the substitution matched itself.
  Write source through the editing tools. If a backslash must go through Python, build it as
  `bytes([92])` and never as an escape. CI now fails on any carriage return in a tracked text file.
- **A multi-line search-and-replace over a source file matches nothing.** Working copies here are
  CRLF, so a pattern written with `\n` line endings is simply absent. The danger is not the failure,
  it is that a *series* of replacements half-applies: the single-line ones succeed while the
  multi-line ones silently do not, and the script reports success. That is how a descriptor array got
  resized without gaining its new entries, which cost a device lost. Again: use the editing tools.

**Code scanning**

- Alerts on `argv` reaching a path or a subprocess are **dismissed as false positives** in this
  repository, and correctly so while the only person supplying the argument is the person running
  the process. **That reasoning expires in Phase 6** — see Etape 12 in TODO.md.
- **A dismissal is bound to the code it was granted against.** Editing a flagged line re-raises the
  alert as new. Expect that, and do not read it as a regression.
- **Expect a fresh batch whenever a new language first lands on main.** Default setup adds analysers
  automatically.

**Two places where a throw is not an error but a crash**

Both call `std::terminate` outright, with no handler anywhere in the process able to intervene, so
each needs a `catch (...)` that cannot itself throw. Neither is visible to any check this repository
has: the reference render only exercises the success path, and an error path that has never run is
indistinguishable from one that works.

- **Every thread entry point.** An exception leaving one terminates the process. The logger's worker
  is the sharp case — allocating a message can throw `std::bad_alloc`, so under memory pressure the
  first symptom would be the process dying silently, from the component whose whole job is to have
  the last word. A logger that has stopped logging is the right outcome; a logger that has stopped
  the program is not.
- **Every destructor that does work.** A destructor is implicitly `noexcept`, so this is not
  conditional on unwinding — any throw terminates. Joining a thread, sending on a queue and taking a
  lock all qualify, and all three look harmless.

The pattern is in `DeviceIdleGuard` and `RenderThreadGuard` in `main.cpp`, and in `Logger::workerLoop`,
which is nothing but the boundary so that nothing can be added outside it.

**Comments state what is, never what was**

- **No change narration in code.** "This used to be X", "the previous version did Y", "fixed in the
  commit that added Z" — all of it belongs in the commit message and the CHANGELOG, both of which are
  dated and therefore honest. A comment claiming a past is stale the moment the code moves again, and
  it costs a reader attention to work out that it is describing something no longer present.
- **No progress narration either.** "Now handles the empty case", "this is the new path", "added for
  Phase 6" — a comment is read by somebody looking at the code as it stands, for whom "new" is
  meaningless.
- **Explaining why the obvious alternative is wrong is different and welcome.** "Rows rather than a
  matrix, because a matrix in a buffer means agreeing with the compiler about row-major" ages well:
  it is about the design, not about its history. The test is whether the sentence would still make
  sense to someone who had never seen an earlier version.

**Pruning documentation**

- A completed etape, or any historical note, **may be deleted once the durable decisions inside it
  live in the code they govern.** That is the test. Apply it before deleting, not after.

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
and post processing, and **Phase 5 — acoustic rays** is done too. **Phase 6 is under way**: Programs
plug in and perceive — ears, eyes, touch, their own voices — through the C ABI in
`libs/program-abi`, and what remains of the phase is in TODO.md's open etapes.

The phase table is canonical in [`TODO.md` § Roadmap](../TODO.md), which also holds the active
etapes. Read it there rather than duplicating it here.
