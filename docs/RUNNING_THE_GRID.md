# Running the Grid

How to stand the whole ecosystem up on one machine and watch a creature live in it: the world,
the window, a host with a Program, the record it leaves, and the tools that judge that record.
This is the guide for the day everything is built; how to build each repository is its own
`docs/DEV_ENV_SETUP.md` (linked at the end).

---

## What runs where

The Grid is one world with many eyes on it. Four repositories, three kinds of process:

| Process | From | What it is | Talks to |
|---------|------|------------|----------|
| `master-control` | [master-control](https://github.com/ai-quokka-wannabe/master-control) | **The world.** One authoritative, deviceless process: the tick, the roster of record, the physics, the validator, the logs. Runs headless anywhere | Every client, over TCP |
| `TronGridLite --window` | [tron-grid-lite](https://github.com/ai-quokka-wannabe/tron-grid-lite) | **The User's eyes.** A spectator: draws the world Master Control tells of, with sound. Needs a GPU and a display | Master Control |
| `TronGridLite --program <name>` | tron-grid-lite | **A creature host.** Loads a Program from `programs/`, rezzes its body into the world, answers its senses, sends its intents. Needs a GPU for the creature's eyes, no display | Master Control |
| `programs/rc_worm` | [rc-worm](https://github.com/ai-quokka-wannabe/rc-worm) | **The first Program**: a chain of eight neon icosahedra, remote-controlled from a panel it opens itself | The host that loaded it, through the Program ABI |
| `link.dll` / `liblink.so` | [the link repository](https://github.com/ai-quokka-wannabe/link) | **The wire.** The one protocol library every process above loads from beside its own executable. Never run by itself | — |

Nothing here needs the internet, an account, or another machine. The default is everything on
`127.0.0.1`.

---

## Build order

Link is never built by hand: each consumer's build system builds it from its `external/link`
submodule and puts the library beside the executable, which is the only place anything looks
for it. So the order is simply:

1. **master-control** — `cargo build --release` in its clone (`target/release/master-control`,
   with `link` beside it).
2. **tron-grid-lite** — a preset build, for example `cmake --workflow --preset windows-msvc`
   (`build/windows-msvc/src/Release/TronGridLite`, with `link` beside it and a `programs/`
   directory next to it).
3. **rc-worm** — a preset build, then an install that deploys the worm and, on Windows, the
   Qt it needs beside it:

   ```text
   cmake --workflow --preset windows-msvc
   cmake --install build/windows-msvc --config Release --prefix build/windows-msvc-deploy
   python tools/check_deploy.py build/windows-msvc-deploy/programs
   ```

   Copy everything in `build/windows-msvc-deploy/programs/` into the Grid's `programs/`
   directory (beside `TronGridLite`). The Grid loads a Program with its own directory on the
   search path, so no Qt needs to be on the `PATH`.

Every repository's `docs/DEV_ENV_SETUP.md` has the exact toolchain each build needs. Use the
same compiler family for the Grid and the worm on Windows where you can (MSVC with MSVC,
MinGW with MinGW): the Program ABI is plain C and mixing works, but one runtime is one less
thing to wonder about.

Check what the Grid would accept before starting a world:

```text
TronGridLite --list-programs
```

It loads every library in `programs/` and says, by name, which are usable and why the others
are not (a Program built against another ABI version is the usual reason — rebuild it against
the header the Grid was built with).

---

## The constellation on one machine

Three terminals, in this order. Each process prints what it is doing; keep all three visible.

### 1. The world

```text
master-control 47000 --disk life.disk --log life.log
```

- The positional argument is the port (default `30702`, Tron's number; any free port works).
- `--disk <path>` records the world in the wire's own bytes — **the Disk** — rolling over to
  `<stem>.0002.disk` and on at `--disk-roll <MiB>` (48 by default, `0` never).
- `--log <path>` writes the **input log**: every intent judged and applied, every rez, and a
  state hash on the beat. Both are optional; a life worth keeping has both.
- `--verbose` makes the world talkative; `--version` prints its build stamp and Link protocol.

It greets: `Greetings, Programs! Master Control listening on port 47000.` and ticks at a fixed
32 Hz from then on, whether or not anyone is connected.

### 2. The window

```text
TronGridLite 127.0.0.1:47000 --window
```

The live view: the creatures Master Control tells of, drawn interpolated between the two newest
ticks, with sound (`--mute` for none). Fly with `W`/`A`/`S`/`D`, `Q`/`E` down and up, look with
the mouse; `Tab` captures the cursor. The eye starts at `(0, 6, 40)` looking down `-Z`; the
spawn pad is near the origin, some forty metres ahead.

If nobody is listening at the address, the window refuses loudly — `No Master Control at
127.0.0.1:47000 - is it running?` — and there is deliberately no silent fallback.

### 3. A host, with the worm

```text
TronGridLite 127.0.0.1:47000 --program rc_worm --ticks 3000
```

The host rezzes the worm's body over the wire, the world seats it on the spawn pad, and from
then on every tick the host answers the worm's senses from the world's telling and sends the
worm's intents back for the world's physics to judge. `--ticks N` bounds the run to N world
ticks and then the host leaves politely; without it the host stays until the world ends.

The worm opens **its own panel** from inside the host: eyes, ears (band-by-bin histograms with
the arrivals marked), feel (contacts, forces), and the controls — `W`/`S` forward and back,
`A`/`D` turn, `Space` call, `X` brake, and sliders that hold a course. Drive it: the window shows
the chain undulating across the terraces, and the ears light up with its own scrapes on the
floor. rc-worm's `docs/PANEL.md` explains every panel; `docs/FIRST_LIFE.md` says what to look
for, and what would be a finding worth reporting.

More hosts are more creatures: start another `--program` instance (the same Program or another)
and the world seats it too. A host that tries to wear an identity another host wears is
refused, by name, on its own connection — `REFUSED: another host wears that identity` — and
stops, because there is nothing for it to host.

### Ending a life properly

Press **Ctrl+C in Master Control's window** (or close it). The world finishes the tick in hand,
writes the log's `end` line, closes the Disk with its farewell, hangs up on every client, and
exits 0. The window and the host then leave on their own. A second Ctrl+C ends the process at
once, the old way — and a life ended that way has no end line, which Clu will tell you.

### All three at once (Windows)

rc-worm ships a launcher that starts the three processes in three console windows, waits for
Master Control to end, and runs Clu on what was recorded:

```text
.\tools\first_life.ps1 -Grid <path>\TronGridLite.exe -MasterControl <path>\master-control.exe
```

`-Out <dir>` names where the Disk and the log go (default `.\life-<timestamp>`), `-Port`
the port (default 47000). It refuses, in words, if the Grid's `programs/` does not hold the
deployed worm.

---

## After the life: the record, judged

Two files were written. Both are whole — every tick is written as it happens — and both can be
checked.

**Clu re-simulates the log** and compares its hashes with what the world wrote, on the beat:

```text
master-control clu life.log life.disk
```

An honest log agrees to the last hash: `Clu: 3040 ticks re-simulated, 95 hashes agreed - the
log replays to the world it describes.` A lie — an intent changed, a line removed — is named at
the first hash after it, and with the Disk beside the log, the disagreeing creature and number
are named with the recorded and re-simulated bits side by side. A log from another world, or
another Link protocol, is refused in words; a log from another *build* of Master Control is
re-simulated with a warning naming both builds. Since the world owns its transcendentals, a log
recorded on one machine replays bit for bit on any other running the same build.

**The window replays the Disk**, at the speed the world ran, with no Master Control needed:

```text
TronGridLite --replay life.disk
```

The same view as `--window`, from the record instead of the wire. A Disk that rolled over is
replayed one file at a time; each file stands alone.

---

## Checking a machine without a world

These answer a question rather than draw a picture, and none needs Master Control:

| Command | Question |
|---------|----------|
| `TronGridLite --debug` | Does the Grid render here? The built-in stage, free flight, no world, no Program |
| `TronGridLite --list-gpus` | Which Vulkan devices are here, and which would be chosen? |
| `TronGridLite --list-programs` | Which Programs are in `programs/`, and which would load? Needs no device |
| `TronGridLite --verify-acoustics` / `--verify-scene` / `--verify-senses` | Do the shaders agree with the host's own arithmetic? These run in CI on lavapipe; they need a device but no display |
| `TronGridLite --benchmark` | What does each GPU pass cost, on real hardware? |
| `TronGridLite --version` / `master-control --version` | Which Grid and which Link protocol; which world build |
| `master-control clu <log> [<disk>]` | Does this record replay to the world it describes? |

---

## Addresses, ports and trust

- The address is always the first argument, `host:port`; the default is `127.0.0.1:30702`.
  Master Control's positional argument is its port.
- Two instances of the Grid on one machine are the normal case: one window, one or more hosts.
  One instance never plays two roles — `--window --program` is refused with the reason.
- Master Control listens on all interfaces; today the trust tier is **the local machine**. It
  validates everything that reaches it and refuses by name, but it does not authenticate: put it
  on a network you own, not on the internet. The long-term invitation (hosting strangers'
  Programs) is a later tier, ruled in
  [TOPOLOGY.md](TOPOLOGY.md) § Trust, in writing.
- Every mismatch is refused **at the handshake, in words**: a client whose Link protocol differs
  (`this end speaks 8, the peer 7`), a client from another world (the world fingerprint), a
  Program built against another ABI. Nothing negotiates; everything says which two versions
  disagreed, so the fix is always "rebuild that one".

---

## Troubleshooting

| Symptom | Cause, and what to do |
|---------|-----------------------|
| `No Master Control at 127.0.0.1:47000 - is it running?` | Start `master-control 47000` first, or fix the port |
| The handshake is refused naming two protocol versions | One side was built against another Link. Rebuild the older one; both consumers build Link from their submodules, so `git submodule update --init` and a rebuild is the whole fix |
| `--list-programs` says a Program is `UNUSABLE ... built against ABI version N and this Grid speaks M` | Rebuild the Program against the Grid's current `tgl_program_abi.h` (rc-worm re-vendors it: `python tools/check_abi_vendor.py check --flagship ../tron-grid-lite`) |
| The host starts, the panel never opens | The worm was built with the panel but its Qt is not beside it: deploy with `cmake --install` and copy `programs/*` whole, plugins included, then `tools/check_deploy.py` the directory. On Linux, Qt must be findable (the kit's `lib` on the loader path or beside the library) and a display must exist |
| `Master Control refused the body of creature N: another host wears that identity` | Another host already rezzed that creature id. Give this host a different identity, or stop the other |
| Clu: `the log has no end line - the world did not stop on request` | The world was killed rather than asked to stop. Everything up to the last tick is still valid; next time end it with Ctrl+C in its window |
| Clu refuses the log: another world, or another protocol | The log was made by a different world definition or Link version than this Master Control. Re-simulate with the build that made it |
| The window is black | The Grid is mostly black mirror; fly until neon comes into view. If there is nothing at all, read the log for validation errors |
| `can't keep up - tick N ran long` in Master Control's log | The machine is overloaded (a debug build, a sanitiser run, a busy CPU). Simulation time never stretches; the unpaid wall-clock time is dropped and said so |

---

## Where to read next

- [TOPOLOGY.md](TOPOLOGY.md) — who owns what, why every delegation is the way it is, the
  protocol, determinism and replay, trust.
- [PROGRAM_INTERFACE.md](PROGRAM_INTERFACE.md) — what a Program is told and may do: the senses,
  the actions, the body's shape, the ABI's rules.
- rc-worm's `docs/FIRST_LIFE.md` and `docs/PANEL.md` — the first life, at the keys.
- master-control's `README.md` — Clu, the Disk, the log, the stop on request.
- link's `README.md` — the wire, the fingerprint, the companion rule for wire changes.
- Each repository's `docs/DEV_ENV_SETUP.md` — how to build it, on Windows and on Linux.
