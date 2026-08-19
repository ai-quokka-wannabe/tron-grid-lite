# Topology

How the Grid becomes a world rather than a process: one authoritative server, thin clients in two
roles, creature brains as plugged-in libraries, and a User who only ever visits. This document
assigns every responsibility to a block and justifies each assignment with a named industry
practice or a named disaster it avoids.

The evidence behind it is a five-part audit of shipped-MMO practice — server authority, protocol
and replication, client and spectator architecture, networked audio, and the programmable-creature
lineage from Core War through Screeps — archived as research briefs 11 to 15 in
`AiQuokkaWannabe/research-archive/`, outside this repository by the same convention `TODO.md`
records for earlier research. This file carries the conclusions; the briefs carry the citations in
full.

Everything below lives, and is built, in this repository, with one exception: the protocol
library grows in its own repository (`link`), because the wire between two repositories should
live in neither. Master Control will eventually move to its own repository (`master-control`),
and pieces of this one will follow their owners out; until that day this repository is the single
home of everything else, and extraction is a decision recorded in
[The four repositories](#the-four-repositories) rather than a prerequisite.

---

## The shape

Three operating-system processes and one loaded library, all on one machine for now:

```text
                       Master Control
                (the world server; pure cmd,
                 deviceless, authoritative)
                     /              \
          custom protocol      custom protocol
                   /                  \
   TronGrid Lite, spectator    TronGrid Lite, creature host
   (--connect --window:        (--connect --program:
    window + speakers,          headless, GPU senses,
    free-flight camera)         one Program DLL loaded)
                                        |
                                   rc-worm.dll
                              (the brain; its own Qt 6
                               GUI for control and
                               senses telemetry)
```

The world server is **Master Control** — capitalised as the being it is on the Grid, exactly as
Program and User are; `master-control` in lower case names only its repository. The film's tyrant,
redeemed by good engineering: it holds authority so that every Program's world is fair.

The User floats through the Grid with a window and speakers and owes the world nothing. The
creature host is a senses-and-brain terminal with no window at all. The server is the world. A
Program's own GUI — the worm's Qt telemetry — belongs to the Program and shows only what the
creature senses, which is exactly why the spectator exists: someone has to be able to see the worm
crawl.

Drawn small, this is the grand vision's kernel. Every block grows into its eventual role without
re-architecting because authority is placed correctly from the first commit.

## The doctrine

**The server is the man.** Tim Sweeney's phrase (Unreal networking architecture, 1999) and Raph
Koster's law ("the client is in the hands of the enemy" — hard-earned at Ultima Online) say the
same thing from two directions: the server's state is the only state, and clients submit
*intents*, never results. The audit's strongest single finding is that this project's existing
lifecycle already obeys the doctrine more strictly than several shipped MMOs — WoW historically
trusted client movement and fought speedhacks with surveillance for two decades; FFXIV accepts
client-reported positions; this Grid accepts three floats of desire, sanitises them, clamps them,
and computes everything else itself. That is not a compliment to be enjoyed; it is a property to
be preserved. The audit found the failure pattern behind every client-trust disaster surveyed —
Ultima Online's dupes, The Division's god-modes, GTA Online's decade of griefing, Fall Guys'
flying beans — to be the same sentence: *state the server accepted rather than computed*.

**Intents in, world out, and the uplink stays narrow.** A creature's entire influence on the world
is `desired_forward_speed`, `desired_turn_rate`, `vocalisation_strength` — twelve bytes. The audit
names this the design's strongest security property, the same asymmetry EVE's ESI API ships: rich
sanctioned reads, minimal sanctioned writes. Every future widening of the uplink passes through
the same sanitise-and-clamp discipline or does not happen.

**Under GPL, the client is honest by convention; the server is honest by construction.** A GPL
client can be modified by anyone — that is the licence working as intended — so client-side
enforcement (WoW's Warden, sealed clients, attestation) is technically void and philosophically
wrong here. All integrity lives in what the server sends and what it accepts. The open-source RTS
communities' concession that lockstep plus open clients makes information cheats socially rather
than technically regulable is the controlling precedent: *the client cannot hold secrets, ever* —
so the long-term lever is sending less, not hiding better.

## Who owns what

| Responsibility | Block | Why — the practice or the disaster |
|---|---|---|
| Physics, contacts, ground truth | Master Control | The authoritative-server doctrine (Sweeney, Koster); every client-physics failure surveyed (Fall Guys, The Division) |
| Poses, velocities, actuators of record | Master Control | Exclusive write access per entity (the SpatialOS formalisation) |
| The tick counter, as u64 | Master Control | Sim time derived as `tick × dt`, never float-accumulated; the tick-indexed log is the replay |
| Action validation (sanitise, clamp, dedupe by tick, sender-owns-creature) | Master Control | The host's copy is convenience; the server's copy is the law. Battlecode's meter exploits show the validator itself needs adversarial tests |
| The RNG of record | Master Control | Synchronised seeds were mandatory even in 1500 Archers; client entropy never enters the world |
| The action log and the state log | Master Control | StarCraft/Factorio (input replay) and Overwatch/SourceTV (state-stream replay) — both, because inputs pin the binary while state survives version drift |
| Tick pacing against the wall clock | Master Control | dt is sacred; the wall clock is the degree of freedom. On overrun, later ticks run late — simulation time never stretches (EVE dilates, Minecraft skips, Screeps floats; all chose "never bend dt") |
| Session join/leave, rez-time model intake and re-validation | Master Control | `copyValidatedModel`'s logic runs where the roster-of-record lives; a model blob is the one variable-size client input — the Dark Souls III RCE shape — so its caps and index checks are server-side law |
| GPU sense rendering (eyes, irradiance) | creature host | The hardware is there; senses are advisory input to the local brain, never reported upstream as fact |
| Acoustic sense synthesis (ears, calls) | creature host | Host-side today by the same delegation — with the deviation honestly recorded below |
| Calling `program_tick`; the DLL's whole lifecycle | creature host | One DLL per process (the existing `ProgramLib` guard becomes the process boundary); BWAPI's entire competitive history ran brains in-process on the operator's own machine |
| The worm's control/telemetry GUI | the Program (Qt, its own thread) | A Program's window is its own business — the Grid provides nothing |
| Rendering the world; the free-flight camera | spectator | Presentation is the canonical delegation; the camera never replicates |
| Interpolation buffer (2–3 ticks) | spectator | Source's `cl_interp` canon scaled to 32 Hz; without it, 32 Hz state under a high-refresh renderer visibly steps |
| Audio rendering (speakers) | spectator | The industry split: replicate events, render sound client-side (Overwatch's "play by sound"); see [The spectator](#the-spectator) |
| Prediction, reconciliation, lag compensation | **nobody** | They exist for human reflexes in the loop; there are none. A pure spectator interpolates; a creature senses authoritative state, full stop |
| Persistence, accounts, encryption, interest management | **nobody yet** | Deferred with written triggers — see [Deferred, with triggers](#deferred-with-triggers) |

## One tick, across the wire

The existing lifecycle survives networking almost untouched, because its two properties were
accidentally network-shaped from the start:

1. **Master Control** steps physics for the whole roster from the actions staged last tick
   (`stepBody` is already a free function of a creature and the ground), then broadcasts
   `TICK_STATE`: every creature's pose, velocity and actuators, tick-stamped.
2. **Every client** receives the settled world — the network delivering exactly what
   `SensesSource::beginTick` has delivered in-process since the calls landed: the roster physics
   has finished with.
3. **The creature host** updates its stage, renders its creature's senses from authoritative
   tick-N state (never interpolated — a brain fed fabricated poses is the divergence class the
   audit warns about), calls `program_tick`, and sends `ACTIONS` tagged for tick N+1.
4. **Master Control** accepts the latest actions with tick ≤ N+1 as it steps; anything arriving
   later is discarded. A missing action means the creature coasts — which is not a networking
   policy but the ABI's existing sentence: a Program that writes nothing coasts.

The one-tick action delay this implies is not a compromise: it is the staged-actions contract the
ABI has documented since physics landed, and it is the 1500 Archers insight (schedule commands for
a future turn and latency vanishes into the schedule) at a one-tick horizon. At 32 Hz on
localhost, a round trip of well under a millisecond hides inside a 31.25 ms budget; the honest
risk is the host's own sense-render-plus-inference time, which gets an instrument, not an
assumption. If misses ever become non-rare, the published rule changes once, for everyone, to a
constant "sense at N, apply at N+2" — a fixed sensorimotor delay is learnable by a creature, a
jittery one is noise.

## The protocol

Custom, binary, and built with this repository's existing disciplines — because the audit found
that the places protocols actually fail are the places this codebase already has mechanisms for.

- **Transport, v1: TCP, one connection per client, `TCP_NODELAY` on both ends, one coalesced
  write per tick.** WoW, EVE, FFXIV and Minecraft all shipped worlds on plain TCP; Minecraft is
  the exact scale-point precedent (tiny payloads, 20 Hz, authoritative server). At roughly 500
  bytes of world state per tick — a dozen creatures at ~40 bytes each — the entire server
  broadcast is ~16 KB/s, three orders of magnitude below where the delta-compression and
  reliability machinery of Quake 3 and Source earns its keep. The famous WoW/Nagle/delayed-ACK
  pathology is avoided by construction: NODELAY plus one write per tick.
- **But UDP semantics at the message layer, from day one.** Every message self-contained and
  tick-stamped; nothing means anything by its position in the stream; the server executes the
  *latest* actions with tick ≤ T and discards stragglers, so TCP's eventual delivery can never
  inject stale intent. When a measured trigger fires — internet loss making motion stutter
  through retransmit stalls, or a human-piloted creature — the transport swaps to the Gaffer
  stack (sequence/ack/ack-bits, actions repeated across datagrams as Tribes repeated its moves,
  ≤1200-byte datagrams) without the messages above it changing.
- **Framing**: `u16 length | u8 type`, little-endian, then a fixed-width no-padding POD payload —
  the ABI's layout discipline extended to the wire, with the same static-assert pinning. The
  receiver validates length against a per-type maximum *before* any copy: the audit's protocol
  postmortems (Dark Souls III's remote-code execution, CS:GO's pre-auth overflows) all lived in
  parsers that trusted a length field. This is the one part of the protocol that is gold-plated
  on day one, localhost or not, because it is correctness, not security theatre.
- **Handshake**: magic, then a protocol fingerprint produced by the same fingerprint tooling that
  guards the ABI header; mismatch earns a human-readable refusal and a closed connection —
  refusal, not negotiation, exactly as `tglGetProgramVTable` behaves.
- **Messages**: `HELLO`/`WELCOME` (identity, tick rate, current tick), `REZ` (a creature's
  identity, descriptor and render model — client to server at hosting time, server to everyone on
  spawn and to every late joiner), `TICK_STATE` (the full world, every tick, every client — no
  deltas, no acks, no area-of-interest at this scale), `ACTIONS`, `EVENT` (vocalisations and
  kin, tick-stamped notifications, never load-bearing state), `DEREZ`, `PING`/`PONG`, `BYE`.
  Late join is not a special case: `WELCOME`, the `REZ` of every live creature, then the next
  `TICK_STATE` — Quake 3's gamestate-then-snapshots in one code path. A spectator is a client
  that never sends `ACTIONS`; observers fall out of the broadcast for free, which is the
  SourceTV insight.
- **One structural rule bought from the Screeps lineage**: the broadcast is per-subscriber
  filterable from day one. Not filtered — v1 sends everyone everything — but the code path is
  "compose this subscriber's view", not "write the one global buffer to all sockets", so the day
  sense-integrity triggers fire (below), interest management is a filter dropped into an
  existing seam rather than a protocol redesign.
- **Two logs on the server, both nearly free**: the state log (the exact broadcast bytes — a
  replay viewer is a client whose socket is a file, the Overwatch/SourceTV architecture, and it
  keeps working across sim changes) and the input log (seed, config, every accepted action —
  Factorio-style deterministic re-simulation), with a periodic state hash in the log so replay
  divergence is detected rather than trusted. This is Etape 16's hash, promoted to the world.

## The spectator

A playout buffer with a camera and speakers — and deliberately nothing more. The audit's
watch-item is the temptation to let a spectator "smooth things out" by simulating locally; that
is the rubber-banding class, and the answer is the discipline every observer system shipped:
interpolate buffered truth, hold the last pose on starvation, never invent.

- **Motion**: a snapshot ring buffer keyed by server tick; render 2–3 ticks in the past
  (62–94 ms — Source's canonical comfort zone, sized to survive two lost packets); linear
  interpolation of positions, shortest-arc for yaw. No prediction — a spectator has no avatar,
  so there is nothing to predict. The window path grows the same movable-instance machinery the
  senses pass already has, plus a world-generation term in its redraw gate, both long
  anticipated in the code's own comments.
- **Sound**: the Grid finally reaches the User's ears, and the ABI's no-auralisation clause
  survives intact — because auralisation was refused on the *creature* path, and the spectator
  is presentation. The server replicates acoustic *events* (a call: tick, position, strength;
  the hum: ambient state), and the spectator synthesises audibly — the exact split Overwatch's
  "play by sound" and Halo's replication design ship. Rendering is parametric, not convolution:
  the acoustic model's energy-per-band impulse responses have no phase, and the shipped
  precedent for exactly this data shape is Microsoft's Project Acoustics — bake physics, drive
  cheap runtime parameters (onset delay from the first-arrival bin, gain from direct energy,
  per-band weighting, an exponential tail from the decay). Output is a hand-rolled WASAPI
  shared-mode renderer — Windows-SDK-only, zero dependencies, an honest 500–800 lines — with
  Bencina's real-time rules as law on the audio thread: no locks, no allocation, no I/O,
  silence-fill on starvation, events crossing on a preallocated ring. Pings are synthesised
  wavetables, the hum a procedural oscillator bank; no assets, no files. HRTF, Doppler and
  spectator-side occlusion are deferred luxuries.
- **The CS:GO coaching bug is the standing warning**: spectator privilege is a server-enforced
  role, not a client UI state. The spectator connects as a role that cannot submit actions, and
  the eventual mitigation ladder (below) filters *host* feeds while spectator feeds stay full —
  the Screeps split, where humans see everything and code sees only what its senses deliver.

## The creature host

TronGrid Lite headless: no window, no swapchain, a GPU for eyes, the host acoustics for ears, and
exactly one Program DLL — the existing one-library-per-process guard maturing into the topology's
scaling law: more creatures, more host processes, one brain each.

- Senses come from authoritative tick-N state only. The host never interpolates, extrapolates or
  predicts on the sense path.
- The pipeline budget — receive, stage, render senses, run the brain, send actions — is
  instrumented against the 31.25 ms tick, and the miss counter is surfaced per host. Misses
  coast, by the ABI's own semantics; the Overwatch and Rocket League answer (repeat the last
  input, never stall the world, never rewind) agrees.
- The Firefox "Lorentz" insight arrives free of ABI changes whenever wanted: a supervisor that
  relaunches a crashed host is crash isolation without security isolation, and it is the cheap
  90% of what process isolation ever buys here — since the accepted risk is the owner's own
  machine, per the Ardour precedent that in-process plugins are a defensible choice when you own
  the deadline.
- The worm's Qt GUI runs inside the DLL on its own thread, as always planned; the host neither
  knows nor cares.

## Trust, in writing

The threat model, stated so it cannot ossify into an assumption nobody remembers choosing.

**v1 runs on one machine, on localhost, among processes the owner installed.** Therefore: no TLS,
no accounts, no rate limiting beyond sanity caps, no sandboxing of the owner's own DLL. These are
deferrals with triggers, not gaps — the trigger for the whole security tier is *the first
connection that is not 127.0.0.1*.

**What is NOT deferred**, because it is correctness wearing a security coat, and because the
audit's named disasters bit precisely here:

- Length-validated, bounds-checked parsing of every message, both directions — the server
  survives a malformed host and the host survives a malformed server (the Source-invite RCE
  taught that the wire bites both ways).
- Server-side sanitise-and-clamp as the only path into the world; the validator itself gets
  adversarial tests (NaN, infinities, denormals, replayed ticks, out-of-range indices).
- Rez-model caps and index checks server-side — the one variable-size client input.
- Sender-owns-creature on every action; one action stream per creature per tick.
- Creature-authored strings (a future name, a log line) are escaped by default in every surface
  that renders them — the Screeps Steam-client RCE was creep names containing script, and its
  deeper lesson was the posture: this project answers reports, and this document is its stated
  threat model.

**The sense-integrity stance, verbatim, with its triggers.** The creature host receives the full
pose broadcast in order to ray-trace its creature's senses locally; a modified host can therefore
feed its DLL perfect information. This is accepted — the maphack class is structurally
unpreventable with a GPL client holding world state, and there is nobody to cheat on a private
Grid — until any of these fires: a ranked or prized event; a host operated outside the
maintainers' circle of trust; credible suspicion of a brain acting on information it could not
have perceived; a world economy where scouting confers advantage. When one fires, the ladder is
climbed cheapest-first: per-host interest filtering in the already-filterable broadcast
(Valorant's fog: the data never arrives); post-hoc statistical audit against the logged poses and
the deterministic sense spec (the chess.com model — the only anti-cheat fully compatible with a
GPL client); server-computed hearing (cheap long before vision ever could move). Attestation and
client scanning are rejected permanently.

**Brains as artefacts.** A native DLL is a malware distribution format the moment it is shared —
the Minecraft fractureiser incident and the Cities: Skylines auto-updater sabotage are the
ecosystem's scars. Policy from the first shared brain: brains publish as source; no auto-update
mechanisms in brains, ever; and when the grand vision needs strangers' brains running on
strangers' hosts, the second brain format is WebAssembly with fuel metering (Battlecode's
deterministic budgets at near-native speed), while foreign native DLLs never execute on the
server or auto-ship to other owners' machines — .NET Terrarium tried exactly that, and
Microsoft's own retirement of its sandbox is the closing argument.

## Determinism and replay, scoped

The claims survive networking and come out sharper, provided they are stated precisely:

- **The world replays bit-identically** from (seed, initial state, action log) on the server's
  build — the same per-build, per-machine scope the repository has always claimed, now pinned to
  one process that never touches a GPU. Cross-machine floating-point divergence, the ruin of
  lockstep RTS replays, is architecturally irrelevant here because only one machine ever
  simulates.
- **The minds do not replay.** Actions were produced by brains fed GPU-rendered senses on
  whatever hardware the host had; the same seed run live again yields a different history. The
  log reproduces the world *given* the actions; it does not reproduce the creatures' decisions.
  Written here so the stronger claim is never accidentally made.
- **Host-side acoustics is world physics computed by a client**, and that is a real deviation
  from "the server is the game", accepted deliberately: the acoustic model is normatively
  specified (ACOUSTICS.md and the header constants), version-pinned in the handshake, and hosts
  are conformant renderers of it. If conformance ever matters adversarially, hearing is the
  first sense that moves server-side.
- **The tick-overrun sentence**, written before it is needed: if a tick exceeds 31.25 ms of wall
  time, subsequent ticks run late; simulation time never stretches; the log is tick-indexed and
  unaffected. dt is sacred; the wall clock is the degree of freedom. That one sentence closes
  the spiral-of-death door.

## Deferred, with triggers

| Deferred | Trigger to build it |
|---|---|
| UDP transport + reliability layer | Measured loss stuttering motion through TCP stalls, or human-piloted control over the internet |
| Delta compression, acked baselines | Per-tick state approaching a quarter of an MTU (~50× today's size) |
| Interest management / area-of-interest | Sense-integrity triggers above, or fan-out beyond ~100 entities / ~16 clients |
| Client-side prediction, lag compensation | A human whose reflexes are in the loop — none exist by design |
| TLS, accounts, tokens, rate limiting | The first connection that is not 127.0.0.1 |
| Persistence (the world survives the server) | The first world anyone regrets losing; until then, snapshot + action log is stronger than a database |
| Spectator delay | Participants able to consume the spectator feed as a side channel |
| Audio beyond parametric pings (HRTF, Doppler, occlusion) | The spectator's ears caring, which is a taste decision, not a measurement |
| WebAssembly brain tier, out-of-process DLL runner | The first community brain from outside the circle of trust |

## The four repositories

| Repository | Eventually owns | Today |
|---|---|---|
| **master-control** | The world: authoritative tick, roster-of-record, validation, broadcast, the logs. Deviceless forever | Created; settings mirrored; documentation and CI furniture landed. The flesh waits for this repository's seams |
| **link** | The wire: the protocol library Master Control and every TronGrid Lite instance load as the same shared binary — Rust behind a plain C ABI, `std` only, zero third-party crates | Created; settings mirrored; scaffolded. The wire contract is its first etape |
| **tron-grid-lite** | The client in both roles, the senses, the renderer, the shared world-definition and physics code the server consumes | Everything else, including the server's future flesh — this document and the split seams land here first |
| **rc-worm** | The first brain: the DLL, its Qt telemetry GUI, eventually the Blender body | Cloned, parked until master-control and link have solid foundations |

The protocol went to its own repository for three reasons with names. The parser is the attack
surface — the Dark Souls III remote-code execution and the CS:GO pre-auth overflows were
memory-safety bugs in C++ packet parsers, which is why the one component that eats hostile bytes
is the organisation's one memory-safe-language component. One implementation loaded by both ends
cannot drift — the duplicated-fact doctrine applied at repository scale, the same reasoning
`static_assert` applies at header scale. And a contract between two parties should live with
neither party — the Program ABI already established that shape inside this repository, and the
wire is the same kind of thing between repositories.

The audit's codebase brief (research-archive brief 11) maps the split seam by seam: `stepBody` is
already a free function of a creature and the ground; `beginTick` is already the settled-roster
boundary the network slides into; the world-definition constants must leave `main.cpp`'s
anonymous namespace for any second consumer to exist; and `src/` finally has the second consumer
that justifies the library target its own test files have been working around.

## The four decisions, taken

The owner delegated these four calls to the implementation (2026-08-11) with one standing
instruction — follow the grand vision — and the grand vision is what each answer serves: a
persistent world of AI creatures that strangers' machines eventually join, watched by humans who
only ever visit.

1. **Authority model: confirmed as written.** Master Control owns physics and truth; clients own
   perception. A world many hosts share has exactly one way to stay one world, and every surveyed
   alternative is a named disaster above.
2. **Transport: TCP first**, with the UDP triggers as written. The vision needs the protocol's
   *semantics* to be datagram-shaped — tick-stamped, latest-wins, self-contained — and they are;
   the socket underneath is swappable the day a measurement demands it.
3. **World roster: dynamic from day one.** Hosts join and leave a running world; a join is a
   `REZ` broadcast and a stage rebuild, a leave is a `DEREZ`, and late arrival is not a special
   case because the protocol never made it one. This is the call the vision decides outright: a
   world that must be restarted to admit a newcomer is a session, and the Grid is not a session.
4. **The defer list: blessed as written.** Every deferral above keeps its trigger, and the
   triggers are the vision's own milestones — the first foreign connection, the first stranger's
   host, the first competitive stake — so deferring now and building at the trigger *is* the
   convergence path, not a detour from it.
