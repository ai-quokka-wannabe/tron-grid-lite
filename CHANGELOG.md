# Changelog

All notable changes to TronGrid Lite are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Every internal link and anchor is checked per pull request, the external ones weekly.**
  Adopted from the owner's `altium-designer-mcp`: `lychee --offline --include-fragments` in
  quick-checks, installed from its pinned release with a checksum rather than through a
  third-party action, so a dead anchor is a red pull request; and `links.yml`, a scheduled
  workflow that follows the external links too, never blocking a merge on a site elsewhere.
- **Direction on the ear: every call arrival carries its exact onset and its radial velocity
  (ABI v6).** Etape 17, the owner's ruling that a creature must be able to sense where a voice
  comes from and that Doppler is realism owed now. `Acoustics::Arrival` records each call
  arrival a delivery makes - the direct path and every validated image, at most sixteen, the
  loudest displacing the faintest - with its onset in seconds (the path over the speed of
  sound, the sub-bin interaural structure a millisecond histogram destroys: two ears 10 cm
  apart hear a caller due east 0.29 ms apart), its radial velocity (positive receding, negative
  approaching, from the settled velocities of caller and listener that now ride on every call
  and every guest telling; images take the direct path's value, an approximation written where
  it is made) and the energy it deposited per band. The ABI carries it as `TglArrival` and
  `TglEarView` grows `arrivals`, `arrival_count` and an explicit `reserved0`; the fingerprint
  moved with the version. The hum records nothing, and the gather's test holds it to that.
  Three breakage rounds: an onset rounded to its bin, the sign of the radial velocity flipped,
  the hum recording arrivals - each caught.
- **The spectator's ears: pings and scratches, spatialised against the camera, with Doppler.**
  The first sound the Grid makes that a human hears. `AudioLib::Mixer` (deviceless, in `grid`)
  turns every `EVENT` into one short parametric sound - a vocalisation is a decaying tone at
  the creature's own pitch (a semitone lattice from A4 by identity, so two creatures are told
  apart by ear), a scratch a burst of low-passed noise as loud as the slide - placed by
  equal-power pan and distance against the camera the render thread publishes per frame, and
  pitch-shifted by the source's replicated velocity along the line to the listener: the
  owner's ruling that Doppler is realism owed now. `AudioLib::Output` is the speakers: WASAPI
  shared mode on its own event-driven thread on Windows, the mixer built at the endpoint's own
  rate; elsewhere a silent drain so the picture behaves alike. A missing endpoint is a warning
  and a mute window, never a refusal to watch; `--mute` opens none. Tested without speakers -
  a sound to the right is louder on the right, a far one quieter, an approaching one higher
  and a receding one lower counted in zero crossings, silence is zeros, a finished voice
  retires - with three breakage rounds; live, the window opened its ears at 96 kHz while a
  walker's footsteps and the caller's pings went through them.
- **Touch knows its face: `TglContact` grows the normal, the depth and the slip (ABI v5, Link
  v6).** The exact-contacts ruling reaches the brain: every contact a Program reads now carries
  the world normal of the face that touched, how deep the body stood past it before the world
  stood it back, and the slip - the body's velocity along the face in its own frame - so a
  planted foot, a landing, a flank along a wall and a brush past another creature all read
  differently. The creature host copies the owner's letter whole (the wire carries the same
  three fields since protocol v6); the ABI layout manifest and fingerprint are re-recorded at
  v5, with the three offsets pinned. The spectator names scratches in its log (`--verbose`),
  footsteps included, until its ears arrive with the audio etape.
- **Clu: `--replay <disk>` plays a Disk back into the window.** A replay viewer is a spectator
  whose socket is a file: the same `WorldClientLib::Client`, opened through Link's
  `replay_open` instead of dialling, the Disk's header judged as a handshake is (another
  contract or another world refused in words), and the same picture - real bodies, the device
  world rebuilt as they arrive and leave. The frames are paced by the recorded dt against the
  wall clock, a telling read ahead of its time held whole until its tick comes, so the Disk
  plays at the speed the world ran and the picture interpolates between tellings exactly as
  live. At the end of the Disk the world stands still - the last telling stays on screen -
  rather than ending. Tested deviceless against a Disk written through the loaded library
  (first telling at once, the second after a dt, the end a standing world); two breakage rounds
  discriminated; proven live with a Disk of a hosted body's whole life.
- **A hosted creature meets the other bodies in the world.** The host keeps every other
  creature's body REZ relayed (never its own, which comes back as the acknowledgement) and, per
  whole telling, where the others stand and whether they call. `Stage::setGuests` appends one
  geometry per shaped guest after the hosted bodies, in creature order; `placeGuests` moves them
  each tick; `GridSensesSource::tellGuests` makes the senses source count their motion as motion
  the caches must respect, place them before any sense is filled, and hear their calls from
  where the world says they stand, through their own hulls. When the guests' shapes change the
  host rebuilds its device world and the tracers that hold it, as the live view does. A guest
  whose REZ carried no rows stands nowhere. Tested deviceless - a guest's body shades the hum, a
  moving guest stales the cache, a guest's call arrives in the bin its distance dictates - with
  three breakage rounds; proven live with two hosts, each rebuilding its world as the other's
  body arrived and left.
- **The live view draws real bodies: a creature wears the shape its REZ carried.** The
  spectator keeps every body REZ told (and DEREZ took) with a generation counter; when the set
  of shapes changes, `WorldStage::setBodies` rebuilds its scene - the Grid, the placeholder,
  then one geometry per shaped body in creature order, its materials appended and its
  triangles' slots rewritten to the global table, exactly as `Stage` does for a hosted body -
  and the event thread hands the render thread a new world to upload together with the
  placements that name it. The render thread rebuilds `World` and `Tracer` under the same lock
  it stages placements under, after one idle of the device, so a placement can never index a
  world that is not on the device yet. A creature whose REZ carried no rows still wears the
  placeholder dart. Rare by construction - a body arriving or leaving - and proven live: the
  modelled driver's body appeared as its own four triangles and two materials, and the world
  went back to baseline when it left, with the window alive throughout. The static modes and the
  reference digest are untouched: only the live view's world ever rebuilds.
- **The creature host: `--program` embodies the Program's creature in Master Control's world.**
  The topology's third box. `WorldHostLib::Host` dials the world as a creature host with the
  world fingerprint, gives each creature a wire identity (the client id's, so two hosts never
  collide), rezzes its bounds and the render model the Grid accepted at rez, and then lives on
  the world's telling: `TICK_STATE` rows land as poses (the actuators' proprioception derived
  from them), the owner's letter `PROPRIOCEPTION` lands as the body's feel - specific force,
  footing, contacts, body frame on the wire and body frame in the ABI, one shape copied - and
  only when a tick is whole (every own body's letter after its rows) do the minds tick, against
  senses answered by the same `radiance` the window renders with. Every intent goes back as
  `ACTIONS` tagged for the next tick with the previous one piggybacked; a stale telling is never
  rewound, a letter for another tick is never applied. The host has no clock of its own;
  `--ticks N` bounds a run, and leaving is a `BYE` the world turns into `DEREZ`. The loading
  probe `--program` used to be lives on in `--list-programs`. Tested against the loaded Link
  playing Master Control (REZ out, telling and letter in, ACTIONS back, the stray letter refused,
  the other world refused), four breakage rounds discriminated - two of which first caught the
  tests themselves - and proven live: the modelled driver embodied for 96 world ticks beside a
  `--window` spectator that watched it arrive and leave.
- **The live view draws the creatures: neon darts on the Grid, moving as the world tells them.**
  The window now renders what TICK_STATE says. A new `WorldStageLib::WorldStage` assembles the
  live view's scene — the Grid plus one shared placeholder body, a low neon dart sized by the
  roster's own body constants with its long slender nose towards -Z because that is the way
  `forwardFor` faces — and turns each telling's interpolated poses into the 144-byte instance
  records the shader reads, capacity-checked against the protocol's own creature cap. The tracer
  grew the dynamic instance path to consume them: one host-visible instance buffer per frame in
  flight, staged the moment the slot's fence has been waited on, while every static mode — the
  debug view, `--record`, `--benchmark` — still binds the world's own immutable upload,
  byte-for-byte the buffer the reference digest has always hashed. The generation counter the
  render loop's ViewState comment always promised for "when creatures move" now exists: a telling
  from Master Control is a reason to draw exactly as a moved camera is, a world gone quiet costs
  no frames, and a minimised window ignores the world's chatter entirely. The event thread
  publishes latest-wins placements at the blend's own pace and stops publishing once the blend
  saturates; a changed creature count always republishes, so a DEREZ between tellings cannot
  leave a ghost standing. Both new host-side properties were broken deliberately once — a
  yaw-less pose transform and a placeholder wearing the floor's material slot — and the suite
  went red exactly where it should. **Master Control's understudy comes with it**:
  `rehearsal_master_control`, a real listening server built from Link's server half (three
  orbiting creatures, one blinking out and back, one calling), proved the whole constellation
  live — connect, interpolate, derez, event, clean shutdown — and stands in until the
  master-control repository exists. In passing: `--debug` was never sizing the tracer to the
  window (the one gate the spectator etape's first half missed), found because the understudy
  made the live view runnable at all.

- **`--window` watches a live world: the spectator dials Master Control, and the Grid is an MMO
  from its very first connection.** The command line takes the ruled shape from
  `docs/TOPOLOGY.md`: one positional argument for where the world is (`host:port`, defaulting to
  `127.0.0.1:30702`), `--window` for the live view, `--debug` for the serverless stage
  inspection that used to be `--window`'s job, plus `--version` (Grid and Link protocol versions
  side by side, because the pair is what compatibility means) and `--verbose`. The new
  `WorldClientLib::Client` walks the whole handshake as a spectator through the loaded Link
  library and keeps the client's living copy of the world: TICK_STATE replaces it whole —
  the broadcast is a full snapshot, so a creature absent from it is gone even if a DEREZ were
  lost — EVENT queues for the ears, DEREZ removes, and every pose interpolates between the two
  newest ticks with yaw taking the shortest arc across ±pi, so a creature crossing the seam
  turns a few degrees rather than almost a full circle (both properties broken deliberately
  once; the suite went red exactly where it should). There is no silent fallback: nobody
  listening at the address is the loud ruled refusal — "No Master Control at … - is it
  running?" — with exit code 1, because a fallen server must never look like an empty world.
  With a live world the window loop's on-demand gate expires — the loop turns at a steady pace
  and drains the wire whole, instead of blocking on the User's hand. The tests play Master
  Control itself through Link's own server half — the first C++ consumer of that surface, and
  the reason it exists, since a hand-written C++ test server would be the second implementation
  of the wire this organisation forbids. The submodule advances to Link's protocol v2
  (port 30702, the server vtable half); README and the shape doctrine follow the new truth.

- **The Grid consumes Link: the first cross-repository organ transplant.** Link — the wire of
  the Grid — arrives as the `external/link` submodule, built by cargo from CMake (rustup stable
  joins the prerequisites; configure refuses by name when the submodule is empty), and is loaded
  at run time exactly as a Program is: one `LoadLibrary`'d symbol, `lnkGetClientVTable`,
  version-refused with `vtable_bytes` checked against this Grid's own header, behind the new
  `LinkLib::Library`. **The residence rule is the owner's**: Link's library lives beside the
  executable, always — no path flag and no search order, so a stale copy elsewhere can never be
  the one that loads; `besideExecutable()` is the single resolver, `copy_link` makes it true,
  and a test exercises the rule end to end rather than reciting it. Compiling the consumer is
  itself half the etape: `lnk_protocol.h` and `lnk_client.h` met MSVC, clang-cl and GCC for the
  first time, so their static asserts finally run under `/WX` and `-Werror` — broken
  deliberately once (a flipped size pin refused the build quoting its own message) — and
  `link_library_tests` pins the loaded library's version, vtable size and fingerprint against
  the submodule's recorded file, the C++ twin of Link's own Rust twin-check. The OS half of
  library loading (`SetThreadErrorMode` suppression, `RTLD_NOW | RTLD_LOCAL`, the
  symbol-to-function-pointer copy, `executablePath`) moved to a shared `LoaderOs` now that the
  second consumer the admission rule waits for exists, so ProgramLib and LinkLib share one
  truth about the platform. The stale C99 mention in the top-level CMake comment is corrected
  to C17 in passing.

- **`docs/TOPOLOGY.md`: the Grid becomes a world — the role-delegation blueprint for the
  three-process architecture.** One authoritative, deviceless world server — **Master Control**,
  capitalised as the being it is, `master-control` naming only its eventual repository — with
  TronGrid Lite as a pure client in two roles — a
  headless creature host computing its one creature's senses beside its one Program DLL, and a
  spectator with a window, speakers and a free-flight camera — and the User only ever visiting.
  Every delegation in the document is justified by a named industry practice or a named disaster
  it avoids, distilled from a five-brief audit of shipped-MMO engineering (server authority and
  the client-trust failure canon; transport and replication practice from Quake 3 to Minecraft;
  spectator and networked-audio architecture; and the programmable-creature lineage from Core
  War through .NET Terrarium to Screeps) archived as research briefs 11–15 beside the earlier
  ones. The audit's headline: the existing lifecycle is already more orthodox than several
  shipped MMOs — staged actions are the 1500-Archers command-turn at a one-tick horizon, so a
  LAN round trip hides inside semantics the ABI has documented since physics landed; zeroed
  actions already define packet loss as coasting; and the twelve-byte action uplink is an
  EVE-ESI-grade read/write asymmetry. The blueprint fixes the v1 protocol (TCP with NODELAY,
  tick-stamped latest-wins messages, fingerprint handshake refusal, full-snapshot broadcast,
  length-validated framing gold-plated on day one, dual state-and-input logs with a periodic
  state hash — Etape 16 promoted to the world), scopes the replay claim honestly (the world
  replays; the minds do not), writes the tick-overrun sentence before it is needed (dt is
  sacred; the wall clock is the degree of freedom), states the sense-integrity trust stance
  verbatim with its revisit triggers, and defers the entire internet-security tier behind one
  trigger: the first connection that is not 127.0.0.1. The four remaining calls were delegated
  by the owner and taken in the grand vision's favour: authority confirmed, TCP first with the
  UDP triggers standing, the world roster dynamic from day one — a world that must restart to
  admit a newcomer is a session, and the Grid is not a session — and the defer list blessed as
  written. The README also now opens the only way this project could: **Greetings, Programs!**

- **The risers stand vertical, and a creature can range a wall with its own voice.** The drawn
  floor stopped being a corner-sampled heightfield: every cell now stands flat at the level of its
  own centre, joined by genuinely vertical walls wherever neighbours differ — 655 of them on the
  shipped landscape, 1,091.7 m² of mirror, every facet of the floor horizontal or vertical with
  nothing tilted in between for a call to be deflected by. The walls come from one list,
  `gridRiserWalls`: the mesh emits its wall triangles from it and `makeGridReflectors` hands the
  same rectangles to the image-source delivery, so the mirror an echo comes back off is the wall
  the picture shows, by construction. Monostatic echolocation is real and under ctest: a test
  stands a caller three metres before a wall of the Grid's own floor and hears the ping come back
  in bin 17. `gridMeshHeight` became piecewise constant to match — the level of the cell under the
  point, a genuine jump at every stepped boundary — so physics walks into a wall rather than up a
  ramp; `plantOnFloor` samples every overlapped cell because a footprint's lowest point stopped
  living on its perimeter; and the neon lies flat on the lips, terrace edges lit above dark
  cliffs, with the tube-endpoint sliver a dead-ending tube pokes into the wall it meets recorded
  as invisible and harmless rather than engineered away. The reference digests moved because the
  picture was meant to move: NVIDIA and Intel re-recorded on the daily machine in this commit, the
  AMD row and the second NVIDIA card pending the weekly machine, and the NVIDIA-to-Intel
  cross-vendor drift figure barely moved across a rebuilt floor — 2.97% to 2.95% — which is the
  stability that figure is kept to watch. ACOUSTICS.md's terraced-floor sections were re-measured
  and rewritten: the "risers are not vertical" analysis became the record of why they now are, the
  90° step-to-floor dihedral genuinely retro-reflects in one plane where the old 157° did nothing,
  and the vertical-riser bullet left the defer list the way a bullet should — built, with its
  trigger's reasoning kept in the code it produced.

- **Creatures stand on the Grid: accepted bodies join the world, and every sense meets them.** The
  staging half of the render model. A new `Stage` assembles the scene once — the Grid at the
  identity, one hierarchy per modelled creature built at rez, one material table with every body's
  slots appended and its triangle indices rewritten to global, acoustic strengths extended by
  written zeros because a hull reflects and does not sing — and per tick moves only placements,
  through the same `flattenInstance` the whole `flatten` is now written in terms of. The senses
  pass keeps its own host-visible instance buffer refreshed per solve, so the `World` stays the
  immutable upload it always was and the window's static path never learns any of this. Bodies are
  real to every sense: another creature's hull shadows the hum, blocks a call's paths, and stands
  in an eye's view. A creature's *own* body is excluded from its own senses — its record blanked
  to a zero node count for its eyes (the flat spelling of a skip the shader already honoured, so
  no shader changed), its instance skipped for its ears and its voice through
  `BvhLib::intersectScene`'s new two-instance skip — because ears sit on bodies and voices leave
  from inside them, and a hull that blocked its own sensors would deafen and gag the body it
  belongs to. The traced-sense caches learned honesty about other bodies: a tick on which any body
  moved stales every cache, so a stationary listener hears a walker pass — pinned by a test in
  which a body slides under a listener, silences its floor, moves away, and the returning hum is
  bit-identical to the baseline. Four breakage rounds — placements frozen, the self-blank dropped,
  the staleness dropped, the gather's skip ignored — each turned exactly its own tests red. The
  reference render digest was re-recorded byte-identical after the `World` refactor, and
  `--verify-acoustics`, `--verify-scene` and `--verify-senses` pass on hardware.

- **One Program library per run, as a mechanism rather than a habit.** `ProgramLib::Library` now
  refuses to load while another library is live in the process, with the reason: the replay claim
  names one tick loop driving one roster, `TglLibraryInfo::creature_count` is promised exact for
  the run, and a second library would put two Programs' process-wide state — locales, signal
  handlers, whatever a GUI toolkit drags in — into one address space with no rule about who wins.
  Sequential loads stay legitimate, which is what `--list-programs` does probing its candidates
  one at a time. The guard immediately caught its first double-load: the Etape 16 hash test ran
  two rosters side by side, and its restructuring into two whole sequential runs is the stronger
  check anyway — the second run now reproduces the first across a full library shutdown and
  reload, which is the shape an actual replay has. The `--program` with `--window` refusal was
  re-verified while the invariant was under the lamp: it stands ahead of window creation, and the
  listing and probe modes both return before a window can exist, so no Program code ever shares a
  process with a swapchain.

- **A Program brings its own body: the render model crosses the ABI at rez.** The one field in the
  interface a Program authors rather than receives, and the inversion is deliberate — the Grid
  decides what a body can do and sense, because capability is physics; what a body looks like
  lives with the creature's own repository, because a Grid that carried every creature's mesh
  would couple its releases to every body ever modelled. `program_rez` gains a `TglRenderModel`
  out-parameter (ABI version 3): vertices, triangles and materials in the Grid's own continuous
  mirror/glow/glass model, every array borrowed for the call exactly as the descriptor's arrays
  are in the other direction, zeroed by the Grid first so a Program that offers nothing has no
  visible body — which is a legitimate body and today's default. The Grid validates the whole
  model before accepting any of it: an index out of range, a non-finite value, a triangle with no
  area or an index of refraction Snell's law cannot bend refuses the rez outright and derezzes
  the handle it already holds, because a silent repair would ship a body its author never saw and
  a poisoned hierarchy fails somewhere else entirely. The accepted copy lives on the creature in
  the ABI's own element types; staging it into the world — the instance, the per-tick transform,
  the senses seeing it — is deliberately the next etape rather than a promise made early. Two new
  fixtures drive the seam: `tgl_driver_modelled` offers a four-vertex pyramid with a glowing tail,
  and `tgl_driver_misshapen` offers the same body with one flank naming vertex nine of a model
  with four, which must refuse the whole rez rather than keep the salvageable part. Both checks
  were broken deliberately once — salvage-instead-of-refuse, and a copy that loses its materials —
  and each round discriminated exactly its own test. Riding along: `docs/PROGRAM_INTERFACE.md`'s
  Vtable section had drifted three ways at once (a version constant under its old name pinned at
  1, a vtable listing without its header members, and a claim that no padding members are written
  by hand); the listing is gone in favour of the header, per the document's own rule.

- **Echolocation: a creature's call sounds on the Grid.** The point-source half of hearing, built
  the moment its written trigger fired — ACOUSTICS.md deferred it against "the vocalisation action
  lands in the ABI", and the action was already there. `Acoustics::deliverCall` enumerates what a
  gather cannot hit: the direct path, graded by a six-point occlusion probe on a sphere of the
  body's own scale so a thin post dims a call rather than silencing it (occlusion sampling, never
  described as diffraction), plus one first-order image per terrace level and per outward box
  face — mirror arithmetic proposing, validation rays disposing, with a hit refused unless it lies
  at the reflection point *in the mirror's own plane*, so a wall standing on a terrace cannot
  answer for the terrace. Same bins, same spreading floor, same air model as the gather, priced by
  the same `depositArrival` the gather now shares rather than by a twin kept in step. The voice
  joined the actuators: staged like every action, sounding next tick with no traction condition,
  collected by the roster's new `SensesSource::beginTick` from physics-settled state — reading
  staged copies mid-loop would hear a tick that never happened — and delivered by
  `GridSensesSource` onto every ear's cached hum, the caller's own first and loudest, which is the
  only readback a voice needs. The hum cache is never written by a call, so the skip licence stays
  exact and a silent tick reads pure gather again, bit for bit. The Grid's reflector list derives
  from the same `PlacedBox` placements the meshes are built from and the same config the floor is
  generated from, so the wall an echo returns off is the wall the window shows. Echolocation as
  built is **bistatic** — the 22.6° risers deflect a caller's own ping skyward, exactly as the
  terraced-floor analysis predicted — and the vertical-riser generator change that would make
  monostatic ranging real stays on the table in ACOUSTICS.md. Twenty-one new ctest cases pin the
  arithmetic to hand-computed scenes (a wall five metres out answers in bin 28; a curtain wall at
  source height dims the probe to exactly one half), and every new check was broken deliberately
  once — bin arithmetic, validation, staging, call collection — and discriminated.

- **A creature is subject to the Grid: gravity, contacts and friction.** The tick loop grew its
  physics step, and the integration policy is worth stating because it was chosen rather than
  defaulted: **analytic where a closed form exists, symplectic where it does not, impulses at the
  non-smooth points.** Ballistic flight uses the exact constant-gravity solution — which is also
  precisely what velocity-Verlet produces for a constant force, so the closed form and the
  symplectic scheme coincide with zero error — and a body walking while turning follows the exact
  circular arc, replacing a chord-walking approximation that measured 7.8 mm of drift in two
  seconds. Traction is a fact about contact: intent moves the body only while it stands on
  something, a body that walks off a terrace falls, feels nothing on the way down (the otolith
  honestly reads zero in free fall), and lands with a thump the support impulse alone cannot
  explain. A standing body feels the floor every tick — mass times gravity times the tick, under
  its feet — and a riser above ankle height is a wall felt on the front face, which makes the
  lattice something a creature can count. Physics collides against `gridMeshHeight`, the analytic
  truth the floor triangles were generated from, through a ground-function seam the tests bind
  synthetic terrain into. The lifecycle's documented order became real: physics advances first for
  the whole roster on the intent staged last tick, so an action takes effect next tick for every
  creature alike — pinned by a test that read -0.515625 instead of -0.5 the moment the order was
  deliberately broken. `TglActions::desired_vertical_speed` and its bound retired with the ABI at
  version 2: height is physics' business now, and a vertical actuator clamped to zero for every
  plausible body was a field the Grid read and nothing could act on. Etape 16's per-tick state
  hash runs under ctest on every push and caught a deliberately injected address-dependent bit.

- **A creature sees the Grid, and the sensor interface is filled.** The second half of the senses:
  `senses.slang` answers one radiance question per sample ray — eye samples and irradiance
  directions in one flat buffer, because both are the same question — through the very `radiance`
  in `grid_optics` the User's window renders with, so what a creature sees cannot drift from what
  the User would see standing in the same place. `SensesTracer` owns the pipeline and a synchronous
  one-shot solve with the same host-visibility barrier the acoustic pass records;
  `GridSensesSource` builds the rays from the pose, maps channels (three-band verbatim, scalar as
  the equal-weight intensity), computes irradiance as the mean over the fixed Fibonacci set, and
  keys the whole visual solve on the pose exactly — a stationary creature re-traces nothing. The
  seam stays Vulkan-free through a `RadianceSolver` interface, which is what lets the GPU-free
  tests drive the entire eye path with arithmetic instead of a device. The first body opens two
  one-sample, one-channel eyes at its head and tail stations and integrates sixty-four sphere
  samples of warmth. `--verify-senses` holds the pass to a host-computable specification — at one
  bounce the Whitted walk collapses to the emission of the first hit, and the two sides agree
  exactly, 512 rays, zero disagreement — and runs in CI beside the other two verification modes.
  It was watched fail first: with the ray buffer's origin and direction swapped, the device answered
  zero everywhere and the check said so. The `--program --ticks` run now needs a device (its eyes
  do) and never a window, and Etape 13's "fill the senses from the tracers that already exist" box
  is ticked.

- **A creature hears the Grid.** The first traced sense crosses the ABI: the roster gained a senses
  seam — a `SensesSource` the run mode hands in, so the tick loop stays free of Vulkan and of the
  Grid alike — and `GridSensesSource` fills `TglEarView`s from `Acoustics::gather`, the same host
  gather the verification modes hold the device to. The first body grew two ears, twenty
  centimetres ahead of and behind its origin, with band edges chosen so each band holds one
  harmonic of the Grid's hum; the `--program --ticks` run now assembles the very Grid the renderer
  draws (one `buildGridTriangles` for both consumers) and gathers hearing from it, still with no
  device. A stationary ear is answered from the previous solve — the gather is pure, so the cached
  answer *is* the right one, keyed exactly on the ear's world position — and the keyed skip the
  acoustics header obliged "whatever eventually drives solves" to perform now exists, pinned by a
  test that watched a stale cache fail to notice a moved creature. Eyes and irradiance are radiance
  questions and arrive with the device pass; a body declaring them today is refused loudly rather
  than left silently unseeing. Senses storage honours the ABI's promises the hard way: 16-byte
  aligned energy, repeated counts asserted against the descriptor, and everything borrowed for one
  `program_tick` only.

- **TestingLib hardened at its two crash-or-silence edges, and taught three things every suite
  wanted.** Two of the changes are fixes by this repository's own rules. A test body throwing
  anything that is not a `std::exception` used to escape `runAll`, propagate out of `main` and
  reach `std::terminate` — remaining tests skipped, no summary, an abort in place of an exit code;
  a `catch (...)` now records it as an ordinary failure. And an empty suite used to report
  "0 failed" and pass, indistinguishable from registration having silently broken; nothing to run
  is now a failure — the did-anything-arrive floor, added to the component every other check
  stands on. Each fix is pinned by its own ctest binary (`testing_empty_suite_test`,
  `testing_foreign_exception_test`), which was built first and watched fail against the old
  runner: the empty suite exited green over nothing, and the foreign throw ended in
  "abort() has been called".

  The additions: `TEST_CHECK_CLOSE(a, b, tolerance)`, which fails showing both values, their
  difference and the tolerance at `max_digits10` — four suites hand-roll
  `TEST_CHECK(std::abs(a - b) < eps)` today, whose failure message contains no values at all, and
  they can migrate as they are touched. A NaN anywhere fails it, pinned by a self-test that
  distinguishes `!(difference < tolerance)` from the NaN-blind `difference >= tolerance`.
  `TEST_CHECK_EQUAL` now compares mixed-sign integer pairs by value via `std::cmp_equal`, so
  `checkEqual(-1, ~0u)` fails instead of passing and a `size()` checks against a plain literal
  without a warning-silencing cast. And float diagnostics print at `max_digits10` rather than
  `std::to_string`'s fixed six decimals, which could not express the very difference a tight
  tolerance measures.

- **`Signal::drain()`, a batch dequeue: every pending message out in one lock acquisition.** The
  render loop and the logger's final drain both consumed one message per lock; both now swap the
  whole queue out and process it unlocked, which also bounds a drain even if producers outpace the
  consumer. The logger's worker loop deliberately keeps the `consume()` loop, because `flush()`
  reads the queue's emptiness as its proxy for "everything written" and a batch swap would report
  empty while a whole batch sat unwritten — the reason is written at the call site. The idea came
  from asking whether `Signal` should grow Qt-style blocking delivery; it should not (the
  architecture document's "no blocking `waitAndConsume`" reasoning still holds), but Qt delivering
  posted events as a swapped batch is the half of that design worth taking. Tests now also cover
  the per-producer FIFO guarantee under concurrent producers, which was documented as load-bearing
  for replay and asserted nowhere.

- **`--window`, and with it a command-line-first program.** The Grid no longer creates a window, a
  surface or a swapchain unless the User asks to see it. `Device` accepts a null surface: no present
  queue is sought, `VK_KHR_swapchain` is neither required nor enabled, and a compute-only card with no
  monitor attached is a perfectly good device rather than a refused one.

  This is the shape the project was always described as having. A creature perceives the Grid through
  a senses buffer and never through a swapchain, so a run that hosts Programs needs no display — and
  `--record`, `--benchmark`, `--verify-acoustics` and `--verify-scene` now genuinely run without one,
  over SSH or on a machine with nothing plugged into it.

  **With `--window` there are no creatures.** It is a debug view of the Grid, not a viewport onto a
  simulation, which is what lets the on-demand gate skip whole frames while the User drags a window
  edge: nothing alive can miss a tick. A Program that wants to show its own internals opens its own
  window; the Grid knows nothing about how a Program works inside and provides it no display.

  The acoustic checks now run before the visual passes are built, so a check on hearing does not need
  `trace.spv`, `bloom.spv` or `postprocess.spv` to be present.

  Recorded renders are byte-identical on both GPUs, and device selection is unchanged: headless simply
  omits the hundred-point bonus for a shared graphics-and-present family, which no device can earn when
  none presents.
- **`--verify-scene`, which is the first thing that ever makes the device trace a transform.**
  Everything before it held the shader to the host at the identity, where a matrix and its transpose
  are the same sixteen numbers, so a layout mistake, a `to_world` used where `to_instance` was meant,
  and a correct implementation are indistinguishable. It is the existing acoustic comparison run with
  the Grid placed at an off-axis rotation and an unround translation, and it agrees as tightly there
  as at the identity: 0.000017% and 0.000170% on the totals.

  Getting there needed `Acoustics::gather` to accept a `BvhLib::Scene`, and the single-hierarchy
  overload is now written in terms of it — a lone hierarchy is a scene of one instance at the
  identity, exactly as it is on the device. One gather rather than two to keep in step.

  **A rotated world cannot be checked against an unrotated copy of itself**, which is what this tried
  first. The direction fan is built in world space, so rotating the world re-aims every ray and a
  finite set of them samples different paths; the few per cent it disagreed by was the sampling
  moving, not the transform being wrong. Two implementations sampling the *same* fan against the
  *same* placement is the comparison that means something.

- **`--benchmark`, which reports what each GPU pass costs** from the device's own timestamps, with no
  file writing and no readback, after ten discarded warm-up frames. The interactive loop has always
  profiled itself but draws on demand, so it cannot be pointed at a fixed amount of work; timing
  `--record` instead buries the pass under PPM writing, with a run-to-run spread of ten per cent
  against this instrument's two.

  With it, the second level's cost is finally a number rather than a shrug: **3.342 ms against
  3.356 ms** for the trace pass at 1280×720, best of three, the two overlapping. It costs nothing
  measurable at one instance, which is what one extra slab test and two affine transforms per segment
  ought to cost against a descent through 24,952 triangles. The whole frame is 3.7 ms, so 270 frames
  a second on the reference GPU.

- **Both senses now trace a two-level hierarchy**, on the GPU as well as the host. `grid_bvh.slang`
  gained `traceScene` and `Instance`, `BvhLib::flatten` turns a `Scene` into the three storage buffers
  a shader can actually index, and the Grid is uploaded as **one instance at the identity** rather
  than as a special case. `trace` survives as a call to the same stack walk at offset zero, so there
  is one traversal in that module and no way for two to drift.

  Making the Grid an ordinary instance is the point. The path a creature body will take is the path
  every frame already takes, so it is exercised by everything this renderer draws rather than by a
  test written for one instance and a comment promising the rest — and at the identity it must
  produce the very same picture, which is a check worth having.

  It does. The recorded render is **byte-identical on both GPUs**, `--verify-acoustics` passes on
  both, and end-to-end recording time is unchanged within noise. That the new path is nevertheless
  live was proved by breaking it: placing the Grid a thousand metres up turns the picture wholly black
  and fails acoustic verification at 100%.

  Two details that are easy to get wrong and were therefore written down where they can be seen.
  **Transforms travel as three rows, not as a `float4x4`** — a matrix in a buffer means agreeing with
  the shader compiler about row-major against column-major, which is exactly the duplicated fact with
  nothing holding the copies together that this repository keeps being bitten by. And **a triangle
  index from the shader is global** where the host's is local to a geometry, because the host has a
  per-geometry array to index and the shader does not.

  The normal is the one thing a transform still costs: the edges are in the instance's frame, so both
  shaders bring the face normal out through the rotation part of `to_world`. Exact for a rigid
  placement, wrong for a non-uniformly scaled one, and nothing places a scaled instance today.

- **The two-level hierarchy exists on the host** (`BvhLib::Scene`, `Instance`, `makeInstance`,
  `intersectScene`). Geometries are built once and shared; instances place them; nothing rebuilds when
  something moves. This is the specification the shader mirrors, in the same relationship `intersect`
  has with the single-level shader path — the arrangement that made `--verify-acoustics` possible.

  **The ray is transformed into instance space without being normalised**, and that is the invariant
  the whole design rests on: leaving the transformed direction alone makes the ray parameter identical
  in both frames, so a distance found in instance space is already a world distance and no rescaling
  is needed. It is also what lets a scaled instance work at all. Normalising it "for tidiness" breaks
  two tests, which is deliberate.

  What a scale does still cost is the normal — under a non-uniform scale the world normal is the
  inverse-transpose, not the transform — so `Hit` returns the instance index and lets the caller
  decide, rather than returning a normal it would have to guess the frame for.

  Four tests, two of them validated by mutation: pointing the transform the wrong way is caught by
  three of them, normalising the direction by two.

- Initial project scaffold: build system, CI, editor config, and hello world.
- Full project infrastructure inherited from [TronGrid](https://github.com/MatejGomboc/tron_grid):
  linting configs (clang-tidy, markdownlint), governance documents (contributing guide,
  code of conduct, security policy, style guide), issue and PR templates, CI workflows
  (main, PR validation, release, cache cleanup), and Claude assistant commands.
- Internal static libraries inherited from TronGrid: `testing`, `signals`, `logging`,
  `math`, and `window` (Win32 / XCB), each with their own test suites.
- `src/` layout with GPL v3 licence headers; volk translation unit included in the build.
- Vulkan foundation inherited from TronGrid and adapted to the lite scope: instance with
  validation and debug messenger, physical/logical device selection, platform surface,
  swapchain with resize handling, VMA allocator wrapper, spectator camera, scene components.
- Material model expressed in code: mirror, emissive and glass, with fields reserved for the
  acoustic properties the shared BVH will need.
- Slang shaders: `postprocess.slang` (ACES tonemap and sRGB encode) and `bloom_downsample.slang`
  (three bloom entry points), compiled and SPIR-V-validated at build time.
- Documentation: `docs/VISION.md`, `docs/ARCHITECTURE.md`, `docs/PROGRAM_INTERFACE.md` and
  `docs/DEV_ENV_SETUP.md`.
- `docs/PERCEPTION.md` — the sensor presets the world renders (`elegans`, `insect-min`,
  `insect-mid`, `insect-high`, `rodent`, `macropod`), the published measurements that set their
  sizes, and twelve binding design rules. Every figure is cited and every biology-to-pixels
  translation is flagged as a translation.
- **Phase 0 milestone reached: a triangle renders in the spectator window** via dynamic
  rendering, verified on a GTX 1650 Ti with validation layers enabled and no errors reported.
- Procedural geometry (`src/geometry.hpp`): flat mirror grid floor, terraced value-noise terrain,
  neon tubes along grid lines with an accent colour on major lines, and a box primitive. Plain
  `std::vector` output with no Vulkan dependency, flat-shaded per-face vertices ready for a BVH.
- GPU timestamp profiler (`src/profiler.hpp`) with per-pass exponential moving averages and a
  once-per-second summary. Self-disables on hardware without timestamp support.
- Spectator controller (`src/spectator.hpp`) — free-flight camera for the human observer, the
  only interactive element in the project.
- `docs/MATERIALS.md` — Fresnel, Snell refraction, the HDR path and tonemapping, kept to what the
  smooth-surface model actually uses.
- **Phase 1 milestone reached: flying through the neon grid** — 24,832 triangles with a depth
  buffer that is recreated alongside the swapchain.

- **Phase 2 milestone reached: the world is ray traced.** `libs/bvh` builds a binned
  surface-area-heuristic hierarchy on the host; `src/trace.slang` walks it in an ordinary compute
  shader with a fixed-size stack and shades every surface with Schlick Fresnel. Measured at
  **4.0 ms per frame at 1280x720 on a GTX 1650 Ti**, a GPU with no ray-tracing extensions at all.
- `libs/bvh` — the hierarchy, with eleven tests. The load-bearing one traces six thousand random
  rays and requires the accelerated traversal to agree with a brute-force sweep exactly.
- `src/tracer.hpp` — uploads the world to device-local storage buffers through a staging copy, owns
  one output image per frame in flight, and records the dispatch.

- **Phase 3 milestone reached: the full ray tree.** Transmissive surfaces split the ray instead of
  only reflecting it, with Snell refraction, total internal reflection and a throughput cutoff.
  The tree is walked with an explicit stack because compute shaders have no recursion, and the
  whole thing remains deterministic. Glass slabs and a glowing translucent column now stand in the
  scene. 13.7 ms at 1280x720 on a GTX 1650 Ti — but see the note below on how that was measured.

- **`--list-gpus`** reports every Vulkan device on the machine, whether it can run the renderer, and
  **why not** if it cannot — "no suitable GPU found" being about the least actionable message a
  renderer can print. **`--gpu <index>`** forces one, overriding a score that otherwise always prefers
  the discrete device. Both exist for cross-vendor testing rather than for configuration.

### Changed

- **The spectator tells Master Control which world it is built from, and both refuse a
  mismatch (Link protocol v4).** `WorldClientLib::worldDefinition()` assembles the
  `LnkWorldDefinition` from `GRID_FLOOR_CONFIG`, `RosterLib::TICK_SECONDS` and
  `BODY_HALF_HEIGHT`; the loaded Link computes the fingerprint (never this side), and it goes
  into the handshake. A Master Control built from a different floor or tick refuses this Grid at
  the door in words naming both fingerprints, and this Grid refuses such a Master Control's
  WELCOME the same way — tested both ways through a rehearsal listening as another world.
  `REZ` frames, now real on the wire, are read and ignored: the spectator still draws the
  shared placeholder dart until real bodies land in their own etape.
- **The Grid speaks protocol v3.** The Link submodule advances to the wire the audit's rulings
  built: ACTIONS resends the previous tick's intent, the keepalive contract is published as
  header constants, and the spectator-ACTIONS refusal is enforced inside the library on both
  ends. The spectator consumes it unchanged — it sends no ACTIONS, so the client half simply
  recompiled against ABI v3, with versions 1 and 2 now refused as history by the loader the
  residence rule already trusts. All three toolchains green, twenty tests on two of them,
  `--version` answers "Link protocol 3", and the live constellation re-proven against the
  rehearsal understudy end to end.

- **The MMO lessons, audited and absorbed whole: TOPOLOGY.md now carries every mechanism the
  briefs taught, and the owner ruled for rigour everywhere.** A pre-heartbeat compliance audit
  re-read all five research briefs against the distillation and the shipped code. The code came
  out clean — the wire validates exactly and before any copy, refusal is never negotiation, the
  spectator interpolates buffered truth and never invents — but the distillation had dropped
  real content, and one genuine fork was hiding: briefs 12 and 14 disagree about a missing
  ACTIONS packet (zero-coast versus repeat-last), and the ABI's coast sentence is about a
  different silence than the network's. The owner's ruling (2026-08-21), under the standing
  principle that **no information may be lost** — this is an embodied-AI project and
  repeatability is the product: silence has three authors now written into TOPOLOGY.md. A
  silent Program said "zero" and brakes; a silent network said nothing, so the server repeats
  the last accepted intent for one tick and every ACTIONS message piggybacks the previous
  tick's intent beside the current one (Tribes' resend, adopted while the wire still changes
  for free); a dead host's creature falls to the neutral reflex and stays embodied — the world
  never waits (Screeps' liveness indifference). Around that ruling, TOPOLOGY.md absorbed the
  audit's recovered mechanisms: the acceptance window [N, N+1) with idempotent dedupe by
  (creature, tick); the fixed-dt accumulator with a max-steps clamp and a loud "can't keep up"
  counter; keepalive as published header constants (PING at one second idle, dead at ten);
  minimal flood posture now (per-connection per-tick quotas, a write-buffer high-water) because
  a runaway local host floods as well as a remote one; Link's server half itself refusing
  ACTIONS from spectator-role connections (the CS:GO coaching lesson, enforced in the one
  shared implementation); FP-environment pinning and the hidden-state determinism checklist for
  Master Control's build; per-creature RNG substreams; traceable identities; three named
  non-goals (no handoffs, no time dilation, no hot database); what TICK_STATE still owes the
  creature host (specific force and contacts) and what REZ's three caps are; the
  world-definition fingerprint WELCOME grows so build skew refuses instead of mis-shading; the
  state-log header, rotation arithmetic, the which-tick-applied field and logged refusals; the
  playout-clock slew, the one-tick extrapolation cap, the never-a-second-acoustic-simulation
  rule and the spectator's two protocol permissions; the ~29 ms host budget line, the
  program_tick watchdog, and the server-side sense spec. **And two new owner rulings landed the
  same day: Doppler is realism owed now** — the spectator pitch-shifts events from replicated
  velocities from the start, off the deferred list — **and creatures must be able to sense a
  voice's direction**: the interaural extension ACOUSTICS.md anticipated is committed (sub-bin
  onsets per ear plus radial velocity per arrival), staged as Etape 17 (since landed). Housekeeping the audit
  caught in passing: DEV_ENV_SETUP's walkthroughs gained the submodule init a fresh clone
  refuses without and now demonstrate `--debug`; the pre-topology "debug window" vocabulary is
  retired across VISION, ARCHITECTURE, PERCEPTION and PROGRAM_INTERFACE; the live view's
  interpolation comment states its honest one-telling depth; and the window's ray depth is now
  pinned to the senses' by static_assert rather than prose. A same-day placement review added
  three rulings to § The four repositories: the `grid` library's **simulated world** (stepBody,
  the clamps, the ground function) follows its owner out to a Rust master-control at its
  Etape 2 — the perceived world stays here forever, since the server never renders — with the
  flagship's local physics mode retiring in favour of dialling a local Master Control;
  `rehearsal_master_control` retires the day the real heartbeat lands; and `program-abi`
  extracts to its own contract repository when rc-worm unpauses, by this repository's own
  lives-with-neither doctrine.

- **The seams the master-control repository has been waiting for: the world definition leaves
  `main.cpp`, and `src/` becomes a library.** The audit's codebase brief mapped the split
  exactly — "the world-definition constants must leave `main.cpp`'s anonymous namespace for any
  second consumer to exist; and `src/` finally has the second consumer that justifies the
  library target its own test files have been working around" — and both seams now exist. The
  Grid the whole world agrees on lives in `src/world_definition.{hpp,cpp}`: `GRID_FLOOR_CONFIG`,
  `makeMaterials`, `buildGridTriangles` and `makeGridReflectors`, with the placements, the neon
  tube config and `plantOnFloor` internal to the one translation unit. The new `grid` static
  library is the single statement of the deviceless core — world definition, geometry,
  acoustics, roster, stage, senses, both loaders, the world client and the live view's stage —
  and every deviceless test target now links it instead of re-compiling translation units one by
  one, which is the drift-prone workaround the audit named. The executable keeps only what needs
  a device: `main.cpp` and the Vulkan translation units. Pure restructuring, licensed the house
  way: the reference digest was re-taken on the 4090 and is byte-identical (`70DDA0E7…`), and
  all twenty tests pass on MSVC and MinGW with clang-cl building clean. With the wire already
  carrying a tick, every gate in the master-control repository's "code-free until the flagship's
  seams exist" rule is now met: the heartbeat can begin.

- **There is deliberately no `--connect` flag, and TOPOLOGY.md now says so.** The owner's
  ruling: once the world lives in Master Control, connection is what a client *is*, not a
  feature it opts into — `--window` and `--program` imply the wire, a client that cannot reach
  Master Control refuses loudly rather than falling back to a world that would look exactly
  like an empty one, and the port becomes a contract constant (`LNK_DEFAULT_PORT`, the owner's
  choice: **30702**, from JA-307020 — Tron's own program designation, the security program
  guarding the doorway into the Grid — landing with the spectator etape). Where the authority lives is not
  a flag either: the address is the plain positional argument — `TronGridLite host:port
  --window`, `TronGridLite host:port --program <name>` — where the world is first, what you are
  there second, defaulting to localhost today and carrying an address anywhere in the world one
  day, with the deferred security tier coming due at exactly that handover. The whole surface
  stays deliberately small: the positional, one role flag, and the citizen's utilities —
  `--version` (Grid and wire versions side by side), `--verbose`, `--list-gpus`,
  `--list-programs`, `--gpu`, and `--debug` (the serverless static stage inspection: spawns its
  own window, loads no Program, needs no world — for debugging the rendering) — with the
  development modes owing the simple surface nothing. The blueprint also now states the training constellation plainly: four things as
  three processes — the animal's DLL inside a `--program` host, a `--window` instance for the
  User, and Master Control — with the spectator attachable and detachable without the animal
  noticing. The serverless static stage view is `--debug`, the owner's ruling: it spawns its
  own window, loads no Program and needs no world — named for its purpose.

- **The flagship is truly oblivious to how the wire is made.** Link grew its own CMake face and
  this repository consumes it: every cargo mention left the build files, replaced by
  `add_subdirectory(external/link)`, the `lnk` header target sitting in the ordinary library
  list beside `math` and `bvh`, and `lnk_copy_beside()` enforcing the residence rule for the
  executable and the test alike. The grep is the proof — the flagship's CMake knows the word
  cargo zero times — and the consequence is the point: if Link were rewritten in another
  language behind the same face, nothing here would change, and nothing here could tell. The
  submodule advances to the face-bearing main.

- **The protocol library has its official name: Link.** Capitalised like Master Control, by the
  owner's word; `link`, lowercase, stays the repository's name, and "the wire of the Grid"
  stays as Link's epithet rather than its name. The vocabulary table, the repository tables and
  STYLE.md § Tron Naming (a fifth ruling) now say so — and Link's own repository pins the
  contract to match: `lnk_protocol.h`, `LNK_PROTOCOL_VERSION`, fingerprint-guarded in the
  flagship's manner.

- **The docs now know the world grew: coherence across the constellation.** The README's
  vocabulary table gains **Master Control** and **the wire**; a **The Four Repositories** section
  — the same heading every sibling README now carries — places the flagship among
  `master-control`, `link` and `rc-worm` and points at TOPOLOGY.md rather than copying it;
  TOPOLOGY.md itself joins the Documentation table it was missing from; STYLE.md § Tron Naming
  rules on Master Control's capitalisation the way it already ruled on Program and User, and
  rules out *MCP*; and the roadmap now says what comes after Phase 6 by pointing at the
  blueprint. The same pass gave link its README and TODO, and master-control its first
  documentation — each repository pointing at the one authority rather than growing a copy of it.

- **The protocol library's home is the `link` repository, not a future `libs/` sibling here.**
  The owner's call, taken with the wire's shape: Rust behind a plain C ABI, `std` only with zero
  third-party crates, one `cdylib` that Master Control and every TronGrid Lite instance load as
  the same binary. The reasons carry names — the parser is the attack surface the audit warned
  about (the Dark Souls III and CS:GO parser holes were memory-safety bugs in exactly this code),
  one implementation loaded by both ends cannot drift, and a contract between two repositories
  should live in neither, as the Program ABI already demonstrates inside this one. TOPOLOGY.md's
  repository table now records four repositories, and the new one was born with this repository's
  settings mirrored onto it, ruleset for ruleset, before any content landed.

- **The Program ABI's C standard is C17, the one complementary to C++20 — and nothing older.**
  C17 — ISO/IEC 9899:2018, also written C18 — is the C standard C++20 itself names as its library
  baseline, so the Grid's side and a C Program's side now quote one era of the language rather
  than two. The C99 negative-array assertion fallback is gone with its compile-check target and
  the version at 4: a pre-C11 toolchain now meets the header's own `#error` rather than an
  unpleasant diagnostic, because a fallback kept for consumers who do not exist is legacy pileup,
  and an ABI header that cannot check its own layout readably is a memory corruption with
  documentation. MSVC ships full `/std:c17`, so no real consumer is dropped. The fixture Programs
  and the manifest tool build at `C_STANDARD 17`; the exercised combinations are the two that
  exist: C17 Program, C++20 Grid.

- **`std::numbers` replaces hand-written mathematical constants where the bits agree.** The
  acoustic two-pi and the first body's right-angle turn bound are now spelled from the C++20
  standard constants — bit-identical to the literals they replace, so no gather, digest or hash
  moves. `Acoustics::GOLDEN_TURN_FRACTION` deliberately stays a literal: `acoustics.slang` carries
  the same literal on the device side, Slang has no `std::numbers` to share, and the header
  documents that pairing as the reason it must not be derived.

- **The Whitted walk moved into a `grid_optics` module, licensed by byte-identity.** `trace.slang`
  promised from its first line that "the same kernel will serve creature sensors", and the eye
  pass is now due — so `Material`, Fresnel and the whole of `radiance` moved into a module both
  pipelines import, with the scene buffers as parameters exactly as `grid_bvh` takes them, so the
  module imposes no binding numbers on anybody. What remains in `trace.slang` is the camera, the
  pixel grid and one `radiance` call per pixel; a creature eye will be the same call per sample,
  from its own pipeline, and the two cannot drift because there is one walk. The reference render
  was re-recorded on the RTX 4090 after the move: `68B384D9…E0501E`, identical to the last byte —
  the same licence that covered the `grid_bvh` extraction before it.

- **BvhLib's host `Hit` and traversal now agree with their shader twins exactly.** The review of
  this library found no defect — it is the best-checked code in the repository — so its round is
  alignment: `Hit` loses the barycentric pair nothing read and the shader's own `Hit` never had;
  the host traversal gains the stack-push size guard the shader and the tests' flat twin both
  carry, so all three walks are uniform and a malformed tree drops a child instead of overflowing
  an array; and `Triangle::geometricNormal()` gains its one true caller, replacing the hand-spelt
  cross product in the acoustic gather. The `intersectScene` skip for an instance naming a missing
  geometry — the case Phase 6's runtime rezzing makes real — is now pinned directly by a test that
  was watched crash on "vector subscript out of range" with the skip removed. Both verification
  modes re-run against the changed host reference on real hardware and still agree.

- **A library's include subdirectory is always its target name.** `signals` now publishes
  `signals/signal.hpp` and `logging` publishes `logging/logger.hpp`, joining the four libraries
  that already matched — so a header is exactly where its target name says it is, and the
  ARCHITECTURE.md paragraph that existed to warn about the two exceptions now states the rule
  instead. Every `#include` in the tree and both STYLE.md examples spell the new paths; CMake
  needed no changes, since both libraries publish `include/` wholesale.
- **The three-row transform layout is now a decision rather than an avoidance.** A `float4x4` was
  tried and it *works*: Slang reads one from a std430 buffer in agreement with `MathLib::Mat4`'s
  column-major storage, giving numbers identical to the row form at the identity and at an angle. The
  rows stay because matrix layout in Slang is a compiler *option* and the mistake it would produce is
  a transpose — which for a rotation is the inverse, so the geometry would still look like geometry
  while being wrong. `--verify-scene` would catch that, but it needs a GPU and therefore never runs in
  CI. Between a layout that cannot go wrong and a layout whose check is manual, this repository takes
  the first.

- **`Acoustics::gather` takes a scene**, and the single-hierarchy overload is written in terms of it
  rather than beside it. One gather to keep correct, and the placement Phase 6 needs already present.

- **The hierarchy question that gated Phase 6 is decided: two levels, not a faster builder.** Putting
  creature bodies in the Grid's single hierarchy means rebuilding it every tick — measured at
  **31 ms with twenty creatures**, most of it spent rebuilding the Grid's own 24,952 triangles, which
  had not moved. A top level over one box per instance costs **0.0031 ms**, a factor of ten thousand,
  because a rigid body's hierarchy is built once when it is rezzed and only its transform changes
  after that.

  **This deletes the etape that was going to fix it.** Etape 10 proposed parallelising the builder;
  sixteen cores might have reached 3 ms, which is a real gain, still a thousand times worse than not
  rebuilding at all, and achieved by occupying the whole machine to recompute something unchanged. It
  was a good solution to a problem that should not exist, and it is now replaced by the two-level
  structure itself.

  It also sharpens the remaining glTF question rather than leaving it independent: rigid segments make
  the second level nearly free, while skinned meshes make it merely much better — about 0.45 ms per
  thousand-triangle body per tick. A world of flat-shaded facets has little use for smooth skinning,
  so the cheap answer and the fitting answer coincide.

  Measurements, method and the calibration against the real Grid are in `docs/ARCHITECTURE.md`
  § One hierarchy today, two when creatures move.

- **The development journal is gone from `TODO.md`, and the file is 606 lines shorter than it was
  this morning.** It was a third copy of history that already had two homes, and it grew faster than
  either. What changed and why lives here; rules worth obeying next time live in `.claude/CLAUDE.md`
  § Hard-won rules, condensed to imperatives, because a lesson written as a story is read once and
  written as a rule is read every session.

  The test applied before deleting any of it was **whether the durable fact already lived in the code
  it governs** — the positive-infinity miss sentinel is explained in both `libs/bvh` and
  `grid_bvh.slang`, the `std::from_chars` argument parsing in `main.cpp`, the rejected compass in
  `docs/PROGRAM_INTERFACE.md`. All of it passed except one item, which was promoted rather than
  deleted: see below.
- **A Phase 6 security obligation was found hiding in the journal and is now Etape 12.** Code
  scanning's path alerts are dismissed as false positives today because the only party who can set
  `--output` is the person who already owns the process. **That reasoning expires** once the creature
  roster resolves Program library paths out of a config file, because a downloaded creature pack
  could write that file — at which point the input genuinely is untrusted and confinement stops being
  theatre. It existed in no other document, and deleting the journal would have deleted it.
- `CONTRIBUTING.md` told contributors to add entries to a `TODO.md` § Journal that no longer exists;
  it now asks for the *why* in the changelog entry instead. Six other documents pointed at the
  journal and now point where the content actually went.

- **Device memory is sub-allocated** (`src/memory_arena.hpp`). A bump allocator hands out offsets into
  a few large blocks instead of taking one `vkAllocateMemory` per resource, and the validation layer's
  sub-allocation complaints go from sixteen to two. The reason was not memory pressure — eighteen
  allocations against a typical cap of 4,096 is under one per cent — but signal to noise: sixteen
  known-benign warnings on every run are sixteen places for a real one to hide, and validation output
  that is routinely ignored is validation that has stopped working.

  No free list, deliberately. Every group of resources here is created together and destroyed
  together — the bloom pyramid and the output images are rebuilt whole on resize, and the Grid's
  buffers live from upload to shutdown — so nothing ever wants to free one image and keep its
  neighbour. The two remaining warnings are the transient staging buffers inside
  `uploadStorageBuffer`, left alone because each exists for one copy and is gone before the function
  returns; sub-allocating a one-shot transfer scratch buys nothing.

- **Phase 5 milestone reached: the Grid can be heard.** Sound is traced through the same hierarchy as
  light, by the same traversal module, and delivered as an impulse response per ear — energy against
  delay, in that ear's own frequency bands. `TglEarDesc`, `TglEarView` and a `vocalisation_strength`
  action are written into `docs/PROGRAM_INTERFACE.md`, which is where the ABI lives until there is a
  header. Hearing and vocalisation arrive together rather than in two breaking changes, because
  **echolocation needs no new sense**: a creature that can emit a sound and hear the reflections
  already has it, so splitting them would have been a change that did nothing on its own.
  `TGL_PROGRAM_ABI_VERSION` does not move — it is pinned at `1u` until 0.1.0.

- **Phase 5's acoustic pass runs on the GPU.** `src/acoustics.slang` mirrors `src/acoustics.hpp` as
  `trace.slang` mirrors `libs/bvh`, importing the same `grid_bvh` module and binding the same two
  `World` buffers with no rebuild and no second structure. **One workgroup owns one ear**, keeping
  that ear's histogram in shared memory as `uint`s — 4 bands by 64 bins is 1 KiB against the 16 KiB
  Vulkan guarantees — so there is no cross-workgroup contention at all. Float atomics are not
  available and are not wanted: integer addition is associative, so the histogram is bit-identical
  however the threads are scheduled, which a float atomic could not have promised and which the
  replay guarantee in `docs/PROGRAM_INTERFACE.md` could not have survived.
- **`--verify-acoustics`** runs the gather on both the host and the device against the real Grid and
  subtracts. It is the acceptance criterion `docs/ACOUSTICS.md` asks for, run by hand because it
  needs a GPU and the build machines have none. On the reference GTX 1650 Ti the two agree to
  **0.00005 %** of the total, with the largest single-bin disagreement at 0.00004 %.

- **Every surface on the Grid is a perfect acoustic mirror.** Absorption removed, transmission
  confirmed absent as a decision rather than a deferral, scattering already absent — so there is no
  acoustic *material* model at all, only a source-strength table of one float per material, non-zero
  only for the two neon tubes. `src/acoustics.cpp` drops from 225 lines to 122 and the ray payload
  loses its throughput scalar, carrying nothing but accumulated path length. Absorption went on its
  own numbers: 0.88 dB over ten bounces at the authored `alpha = 0.02`, nearer 0.2 dB in practice
  because rays on an open plane escape after one or two, against spreading's 26 dB across the range
  cap — and Treble's stated ±0.2 measurement uncertainty on such a coefficient is larger than the
  effect it produced. The impedance model had already said as much: air at 415 rayl against glass at
  13 × 10⁶ gives `|R| = 0.99994`, so the Grid now uses the *derived* answer rather than an authored
  one borrowed from real panes whose losses come from flexure, mounting and edges the Grid does not
  have. This is safe only because the Grid is an open half-space, and that condition is written down
  as the trigger for reopening it.
- **`airAbsorptionDbPerKm` removed**, and with it the extrapolation above 8 kHz that had to be
  flagged as untrustworthy. The four per-band values were always a parameter of the gather; they are
  now authored per listener beside the band edges that come from the same audiogram. Nothing
  simulates air, and the ISO 9613-2 row is cited for whoever authors them.
- **Nothing on the Grid sounds continuously** — the neon pulses, a vocalisation is a call that stops,
  a worm's scrape is sustained but modulated. This costs nothing to honour because the gather computes
  an *impulse response* and a source's behaviour in time is an envelope applied at delivery, so all
  three are one object with different envelopes and the traversal never sees any of it. The reason it
  matters is perceptual rather than computational: a continuous tone carries almost no delay
  information, because every arrival overlaps every other. Onsets are what make a delay measurable,
  which is why bats pulse rather than hum. It yields the right asymmetry for free — a scraping worm is
  easy to detect and hard to range.
- **The documentation debt `docs/ACOUSTICS.md` had been tracking is paid off.** Its correction to
  `docs/PERCEPTION.md` is applied in both places it named: the 6–9 octave band figure is the offline
  one and the Grid uses four, and the prescribed "per-block interpolation" describes a technique this
  repository cannot perform, having no waveform, no blocks and no audio rate — it would also have
  collided with binding rule 6. `docs/VISION.md` no longer claims sound "passes through the same
  glass"; it does not, and behind a slab there is quiet. `README.md` design principle 5 now describes
  coupling by material slot rather than by struct row. The spreading formula in `ACOUSTICS.md` said
  `1/(4πd²)` where the code implements `1/max(d², 1)`; both departures are now documented — the
  constant folds into a relative source scale, and the one-metre floor is the Grid's own unit and is
  what makes "no arrival exceeds its source strength" checkable.

- **Nothing runs without a reason.** The renderer no longer spins at the GPU's maximum rate redrawing
  an image identical to the previous one. It compares the state a frame was drawn from — camera
  position, orientation, field of view, surface size — and sleeps on the render channel's condition
  variable when none of them has moved, which is what a CAD viewport does and what a game loop does
  not. The Grid is far closer to the former: a world that mostly sits still, watched through a window
  that is usually not moving. Measured on the static Grid with the camera still, **0.09 s of CPU over
  12 s of wall clock against 7.56 s on the build that drew unconditionally** — about eighty times
  less, with the GPU dropping off its clocks entirely. The saving is claimed only for a Grid and a
  view that are both standing still; a moving camera draws every pass, as it must. State comparison
  rather than dirty flags, deliberately: a flag is correct only if every writer remembers to raise
  it, and forgetting costs a window that has stopped updating. A swapchain rebuild explicitly forces
  the next frame, because its new images hold
  undefined contents and a resize that left the camera and size unchanged would otherwise look idle
  and leave garbage on screen. There is deliberately no flag to draw unconditionally: the GPU
  profiler averages over frames and a still camera produces none, so a run of frames to average is
  obtained by holding a movement key — which is all such a flag would have done.

- **Every performance figure above was measured with the validation layers on, and they are all too
  slow — the trace pass by about 4.6×.** Debug enables GPU-assisted validation, which instruments the
  shader: it adds a bounds check to every buffer access in the traversal loop, which is the entire
  inner loop of a ray tracer. On the same scene, same GPU and same resolution, the frame is
  **16.9 ms with validation and 3.7 ms without**; the trace pass alone is 16.6 ms against 3.4 ms.
  Post-processing barely moves (0.33 ms against 0.29 ms), because it is a fixed number of texture
  reads per texel with no data-dependent addressing and so has almost nothing for GPU-AV to
  instrument — which is also why the ratio cannot be applied as a blanket correction to the older
  figures. The milestone entries above are left as they were written, because they record what was
  measured at the time; `docs/ACOUSTICS.md`, whose delay table was *computed* from the wrong number,
  has been recalculated.

- **The renderer runs on its own thread.** Not for throughput — the GPU is the bottleneck and a
  thread does not make it faster — but for responsiveness: on Win32 a modal resize drag runs the
  platform's own message loop, so a renderer sharing that thread froze until the User let go of the
  window edge. The event thread keeps the message pump and the cursor, because the pump belongs to
  the thread that created the window and `ShowCursor` is per-thread; everything Vulkan moves across,
  because a queue submission wants one owner. The two communicate through a single
  `SignalsLib::Signal<RenderEvent>` and two atomic flags, collected in one `RenderChannel` struct so
  that the boundary is checkable by reading: if a name is not a member of it, exactly one thread
  touches it. `Window::setEventCallback` — until now unused — is what makes this work, because it
  fires from inside the platform's message handling and so keeps feeding the queue while the pump
  itself is stuck. The interactive frame loop moved out of `main` into `runRenderLoop`.
- `Window::wakeEvents` — the one window method callable from another thread. `waitEvents` otherwise
  sleeps until the User does something, so a render thread that had died could not tell the event
  loop to stop and the window went on looking alive. `PostMessageW(WM_NULL)` on Win32; a
  self-addressed client message carrying `XCB_NONE` on X11, which the existing dispatch already
  discards.

- **There is no ABI versioning, deliberately, until 0.1.0.** `TGL_PROGRAM_ABI_VERSION` is pinned at
  `1u` and stays there. The interface changes whenever it needs to and both sides rebuild; nobody is
  owed backward compatibility by a project that has not reached its first release. The constant is
  kept only for the one job it can still do honestly — catching a stale `.dll` or `.so` loaded
  against struct layouts that have moved underneath it. The rising-number rule had already drifted
  once: the roadmap asked for a bump that a rename had silently spent.

- **BREAKING — the agent ABI is renamed throughout, and `TGL_PROGRAM_ABI_VERSION` is `2u`.** The
  project now uses Tron's vocabulary as its own: a **Program** is the thing that thinks, a
  **creature** is the body it drives, the **User** is the human at the debug window, and the world
  is **the Grid**. `TglBrain` becomes `TglProgram`, `tglGetBrainVTable` becomes
  `tglGetProgramVTable`, and `creature_create` / `creature_tick` / `creature_destroy` become
  `program_rez` / `program_tick` / `program_derez`. `docs/AGENT_INTERFACE.md` is now
  `docs/PROGRAM_INTERFACE.md`. No Program has been written against version 1, so nothing breaks in
  practice.
- `library_init` and `library_shutdown` deliberately keep their plain names. The rule the scheme
  follows is that Tron words name events on the Grid and plain words name events in the operating
  system — those two are `LoadLibrary`/`dlopen` and `FreeLibrary`/`dlclose`, facts about Windows and
  Linux rather than about this world.
- `README.md` gains a vocabulary glossary, and `STYLE.md` records the three rulings that keep it
  stable: Tron's "Program" is a proper noun with the American single-m spelling while British
  "programme" is a different word, the GPL boilerplate stays lowercase and untouched, and "rez" is
  never glossed as "resolve" — "resolution" is used throughout these documents in the pixel sense.

- Unified the material model: `MaterialKind` is gone and `Material` gained a continuous
  `transmission` parameter. Every surface reflects, transmits and emits simultaneously, so there
  is no shader branch and a glowing translucent surface is expressible.
- Reworked the sensory half of the agent ABI, and bumped `TGL_BRAIN_ABI_VERSION` to 1. A body now
  carries an array of `TglEyeDesc`, so a creature may have several eyes, none at all, a channel
  count other than three, and a sample-direction list instead of a raster — all of which the
  sensor presets required and the previous single-RGB-raster field could not express.
- Added two modalities to `TglSenses`: **vestibular** sensing (`specific_force`,
  `angular_velocity`) and **thermoreception** (`irradiance`). Both are nearly free — the numbers
  already exist in the motion integrator and the ray tracer respectively — and the first matters
  particularly in a world of perfect mirrors, where a reflected floor is indistinguishable from
  real space to vision alone.
- Documented that hearing still lacks a prerequisite: nothing in the world currently emits sound.

- `docs/RELATED_WORK.md` — the prior art, with credit to the projects this one stands on:
  OpenWorm and c302 for the worm end of the ladder, CompoundRay and I2Bot for compound-eye
  rendering, NeuroMechFly v2 and the virtual rodent for embodied bodies and brains, and the
  Animal-AI Environment for cognitive testing. Also the short list of what is genuinely unusual
  here, and a plain statement of what this project is not attempting.
- The grid floor is no longer flat. It is displaced by terraced relief — value noise in three
  octaves, quantised to six discrete levels, rising up to five metres over the 128 m extent — ported
  from the original TronGrid's terrain generator. It costs no triangles at all, because it moves
  vertices that already existed, so the hierarchy built over it is unchanged. Optically a curved
  mirror bends what it reflects, so the neon lines draw as arcs and pillar reflections shatter into
  facets across the steps. Acoustically it is the point: a flat plane sends every reflection away at
  the mirror angle and none of it returns, whereas terrace risers standing square to the ground
  throw sound back across the world, which is where Phase 5's echoes will come from.
- Everything standing in the world is now planted on the surface beneath it rather than at y = 0.
- The cinematic camera's mean height rose from 7 m to 10 m so the bottom of its swing clears the
  highest terrace.

### Removed

- **The deletion movement: the physics follows its owner out, and one implementation remains.**
  The companion to master-control's "the physics comes home": `stepBody`, `sanitiseAndClamp`
  and their sixteen tests — traction, arcs, walls, landings, the sanitise quartet, the per-tick
  hash — leave this repository, their guardianship continued by the Rust port and the golden
  vectors this repository's own C++ generated for it. The local `--program --ticks` mode
  retires outright: with no local physics there is no local world to tick, so `--program` is
  now purely the loading probe, and hosting a creature returns with the wire host as its own
  etape, dialling Master Control like every other citizen. What stays of the roster is the
  mind's half — senses in, Program called, intent staged **raw**, because the host's clamp was
  convenience and the server's is the law — with the voice actuator applied from the staged
  intent so the hearing seam still hears one consistent tick. The reference digest was re-taken
  on the 4090 and is byte-identical; clang-cl and GCC each caught orphaned test helpers MSVC
  waved through, exactly as this file promises they will.

- **The understudy retires: `rehearsal_master_control` leaves the tree.** The ruling said it
  would go the day the real heartbeat landed, and the heartbeat has landed — Master Control
  lives in its own repository now (`cargo run --release` there, then `TronGridLite --window`
  here), so the flagship's stand-in server became the second implementation the organisation
  forbids, and second implementations do not get to linger on sentiment. It goes out with full
  honours: it proved the live view before any real server existed, it was the first C++
  consumer of Link's server half, and its scripted world — the orbiters, the blinker, the
  rhythm — lives on inside the heartbeat itself. The `RehearsalMasterControl` class inside
  `world_client_tests` stays: a test double for the client is a different creature from a
  server pretending to be real.

- **The rasteriser's share of MathLib, none of which this renderer calls.** The tracer builds rays
  from the camera's basis vectors and field of view directly, and the BVH is the culling — so
  `projection.hpp` in its entirety (`perspective`, `lookAt`, `viewFromQuaternion`,
  `viewFromSpherical`, `Plane`, `Frustum`, `extractFrustum`, `isInsideFrustum`) had no caller
  outside MathLib's own tests, along with `Quat::slerp`, `Quat::toMat4`, `Mat4::transposed`,
  `Mat4::data()` and the whole of `Vec2`. Inherited from the ancestor renderer and kept by
  momentum; a library's tests are not a user. Deleting `toMat4` also decouples quaternion.hpp from
  matrix.hpp entirely. `Mat4::scale` stays despite being test-only, because a non-uniform scale is
  what makes the `inversed()` round-trip test strong. MathLib shrinks by roughly a third.
- The swapchain's storage-write path: a format search preferring storage-capable formats, a
  two-part capability probe, two warnings, a conditional usage flag, an accessor and a member — all
  serving a "write straight into the swapchain image" strategy the renderer abandoned when it
  retired the rasteriser. It always blits. The image views built alongside them were not merely
  unused but actively wasteful: one created per swapchain image, on every build and every resize,
  and never looked at by anything.
- Twelve accessors with no callers, and the members that existed only to feed them: `Camera`'s view
  and projection matrices along with the near and far clipping planes a ray tracer has no use for,
  `Mesh::boundingRadius`, `Device::graphicsQueueSupportsCompute` (redundant since missing compute
  became fatal), and the whole tunable-but-never-tuned getter/setter surface on the User's camera
  controller.

- Compatibility machinery from the agent ABI: per-struct `struct_size` fields, duplicated
  `abi_version` members, hand-written padding members, and fields reserved for modalities that do
  not exist yet. A single `TGL_BRAIN_ABI_VERSION` check at load time is now the whole mechanism.
  Those devices exist to let mismatched builds keep working, and the interface has no users to
  keep working — they were clutter in every struct.
- The acoustic fields from `Material`. Nothing reads them, and reserving two std430 rows for them
  doubled the material buffer; they arrive when hearing does. `Material` is now exactly two
  16-byte rows with no padding at all, down from four.
- The procedural terrain generators inherited from TronGrid, along with their value-noise
  helpers. They were unused, and terraced heightmaps are the parent project's look rather than
  this one's flat mirror floor. 124 lines of `src/geometry.*` went with them.

- **Phase 4 milestone reached: post processing.** The tracer writes linear radiance into an rgba16f
  target; a new stage runs the bloom pyramid over it, applies the fitted ACES RRT+ODT curve,
  encodes sRGB and optionally vignettes. `src/postprocess.hpp` owns the mip chain, the pipelines
  and the descriptor sets. The whole chain costs 0.31 ms at 1280x720 on a GTX 1650 Ti.
- Exposure moved from the tracer to the tone mapping pass, where it belongs.

- A recording mode (`--record`) that flies a scripted camera loop and writes one image per frame,
  plus `tools/record_flyby.py`, which drives it and encodes the result as a seamless looping GIF.
  The animation at the top of the README is its output.
- `tools/` gained a README, a requirements file and a `.venv` placeholder.

### Fixed

- **Formatting is a gate, under a pinned formatter.** Adopted from the owner's
  `claude-chats-browser`: quick-checks installs `clang-format~=22.1` and runs it `--dry-run
  --Werror` over every tracked C and C++ file, so STYLE.md's claim that the tree is
  clang-format clean is enforced rather than hoped for. The desk runs the same major. The one
  reformat this needed - ten files, indentation only, most of them the last fortnight's - is in
  this change.
- **Eight findings of a bug hunt, fixed, the low and the cosmetic included.** The own ears hear
  the world's call: a creature's `vocalisation` is now the value its own row carries back - what
  Master Control sounded, clamped by its law - never the raw intent, so a Program asking for
  five with a bound of one no longer hears itself five times louder than every other ear does
  (`Roster::tellPose` takes it; the in-process assignment is gone). The intent's tag follows
  the newest telling, whole or not: rows for N+1 already here are proof N+1 is stepped. A guest
  stands on the stage only once the world has placed it, so a relayed body awaiting its first
  row no longer occludes rays at the origin for a tick. A `Roster` whose constructor fails
  partway derezzes every creature it rezzed before the library is shut - held by a new
  fixture, `tgl_driver_refuses_second_rez`, which aborts at shutdown with a creature still
  rezzed, the header's own contract made audible. A roster past 256 creatures is refused
  rather than aliased in the wire identity's low byte. The WASAPI endpoint frees its mix format
  on a refused shape, checks the event handle it waits on, and prints its HRESULTs in hex.
- **The documents describe the repository as it is.** TOPOLOGY no longer awaits a Master Control
  that has moved, a contact model that is built, or a creature-creature pass that exists; its
  four-repositories table says what each holds today. PROGRAM_INTERFACE names `TGL_ABI_VERSION`
  (the constant that exists), tells the truth about `--program` (it hosts, `--list-programs`
  probes) and walks the tick across the wire. ACOUSTICS knows the header exists and that an ear
  view carries arrivals. ARCHITECTURE's tick section says where the physics went and that the
  tick is 32 Hz. TODO's `--window` note now belongs to `--debug`, Etape 13's questions 2, 6
  and 9 are marked decided, and CLAUDE.md readmits `spectator` to the vocabulary. README calls
  Master Control what it is.
- **A slow mind's intent is tagged for the tick the world will step next.** `Host::act` drains
  the wire before it speaks, so when the world told another tick while the Program was
  thinking (a first-tick warm-up, a world rebuilt for a new guest), the intent is tagged for
  the tick after the newest whole telling rather than for one already stepped - which Master
  Control would refuse as stale, on the record. A tick made whole by that drain is handed over
  by the next `poll`, not swallowed; the rehearsal test tells a tick mid-think and checks both,
  and one breakage round (the old reset-then-drain) caught the swallow.
- **A hung Ubuntu mirror cannot eat half an hour of CI any more.** Twice in one afternoon the
  sanitiser jobs sat on `apt-get install libxcb1-dev` until their 30-minute job timeout killed
  them — a package mirror hang, nothing in the tree, while sibling jobs cleared the identical
  step in seconds. Every apt step in every workflow now carries its own one-minute bound (two
  where `mesa-vulkan-drivers` makes the download bigger), with no retries, deliberately: a retry
  can mask a real issue, so a dead mirror is a fast, legible red and rerunning stays a decision.

- **The pedantic pass: the examples were stale even where the prose was true.** The README's
  `--list-programs` example spoke ABI version 1 in a Grid that speaks version 4, so both captured
  blocks were re-taken from a freshly built binary — the listing now shows the real fixtures,
  including a genuinely refused wrong-version library quoting the loader's actual message, and the
  `--list-gpus` example now shows the daily machine's pair, which makes the "exercised on every
  change" sentence beside it true again. The re-take itself vindicated the house rule that catches
  stale binaries: the first capture came from a pre-v4 executable, and "confirm the build succeeded"
  caught it. `--verify-senses` joined the README's mode table it had been missing from since it
  landed, and "the two verification modes run in CI" became the three that actually do.
  `.claude/CLAUDE.md`'s "eleven commits cite byte-identity" was recounted to fifteen, with the
  recount command now inline so the number services itself. TOPOLOGY.md's repository table caught
  up with master-control's furniture landing, and CONTRIBUTING.md's documentation table learnt
  that TOPOLOGY.md exists. Every relative link in every markdown file across all four repositories
  was checked mechanically; all resolve.

- **The docs said "planned" about things that exist: a stale-claims sweep.** `ARCHITECTURE.md`
  carried four sections marked *(Phase 6, planned)* for work that shipped — the marks are gone, and
  each section now says in place how the build resolved it: ground contact went analytic through
  the `GroundFunction` seam rather than through the hierarchy, the tick runs deviceless with its
  presentation phases living in the creature-less windowed mode until the spectator crosses the
  wire, and the sensor interface's normative home is PROGRAM_INTERFACE.md. Its Build System section
  claimed CMake 3.16+ and five presets against the actual 3.25+ and seven; its absences table
  listed "terrain" as absent under a floor with 655 vertical risers, and credited the BVH with the
  ground contact a closed form actually does. `.claude/CLAUDE.md` called the per-tick state hash
  "open work" though it runs under ctest on every push, said "there is no cache" though the hum
  cache has since been built, and its orientation tree had not heard of TOPOLOGY.md, `program-abi`
  or `check_abi_version.py`. `CONTRIBUTING.md` demanded a journal entry its own Documentation
  section says was abolished. Etape 16, all boxes ticked, moved to the completed table with its
  durable reasoning already living in `roster_tests.cpp` and the CI workflow; and Phase 6's status
  is now honest — not pending but under way, with Programs already perceiving.

- **`docs/PROGRAM_INTERFACE.md` § Actions restated a struct and drifted, proving its own rule.**
  The listing still carried `desired_vertical_speed` after ABI v2 retired it, promised logging the
  sanitiser does not do, and repeated the header's claim that "the body's descriptor carries what
  its voice sounds like" — no such member exists; the voice is authored on the Grid's side as the
  ears are. The listing is gone in favour of the header, which is what the document says it does
  everywhere else, and the same false sentence was corrected at `TglActions::vocalisation_strength`
  in the header itself — a comment-only change the fingerprint tool confirms costs nothing.

- **The last three defects in `docs/PROGRAM_INTERFACE.md` are closed, before the physics tick loop
  exists to implement what the document describes.** Threading now states the header's own promise
  — `program_tick` runs serially, in roster order, on one thread fixed for the whole run — where it
  previously offered a parallel-tick licence one bullet away from a single-tick-thread claim; the
  parallel tick is now named as a widening this version does not license, which is exactly the
  change `TGL_ABI_VERSION` documents as a bump. The acoustic reference level is stated once, in the
  header at `TglEarView::energy`: PROGRAM_INTERFACE.md points at it and ACOUSTICS.md derives it,
  because a unit stated in two places can disagree with itself. The lifecycle defect turned out to
  be already fixed in the document — physics advances once for the roster and actions stage after
  every call, both outside the per-creature loop — so its box is ticked with a note that the
  kinematic v1 `Roster::tick` is now the side that grows into the contract when physics lands.
- **An uncovered window could sit showing stale pixels until the camera moved.** The platforms push
  an `Expose` event when the window's contents need drawing — uncovered, remapped, freshly shown —
  but the render loop forwarded it into the spectator, which ignores it, and the idle gate then
  judged the unchanged `ViewState` current and went back to sleep. On a non-composited X server
  nothing else ever repaints. An `Expose` now clears `has_presented`, forcing exactly one frame:
  wrongly deciding a frame is needed costs one redundant frame, which the `ViewState` comment
  already names as the cheap side of that comparison. Alongside it, WindowLib's base-class contract
  finally has tests — the immediate-callback-plus-retained-queue behaviour the renderer's threading
  depends on was asserted nowhere; a stub window now pins delivery, FIFO order, callback reset and
  the close flag, each watched fail against a deliberately broken `pushEvent`.
- **A failed X11 pointer grab is no longer silent.** Another client holding the grab — a screen
  locker, a drag in progress — made `setCursorCaptured` degrade correctly but invisibly: the User
  toggles capture, nothing happens, nothing says why. It now logs a warning, which is also the
  first real use of the logger the window has carried since its construction. Two unreachable
  `return` statements after `throw`s in the XCB constructor went in the same pass.
- **`Logger::flush()` could hang the fatal path, and could let the fatal line garble the last
  write.** Both defects lived in the same line: flush waited for the queue to become *empty*,
  unconditionally. If the worker thread had died — its catch-all exists precisely because
  allocating a message can throw — the queue never drained and `logFatal` spun forever, a hang on
  the one path whose job is to end the process; demonstrated by killing a 15-second timeout before
  the fix and watching the same probe finish in milliseconds after it. And emptiness only proved a
  message had been *taken*, not *written*, so the synchronous fatal line raced the worker's last
  write — observed in practice as `[ERROR] [FATAL] ...` interleaved on one line. flush() now waits
  until every message enqueued before the call has actually been written (a pair of counters; the
  worker increments after each completed write), bails out if the worker has exited, and writes any
  remainder itself — a dead worker writes nothing more, so nothing is left to interleave with. The
  logger's tests previously asserted nothing about output at all; they now capture both streams and
  pin severity routing, per-thread ordering, prefix text, the synchronous fatal write,
  fatal-after-context ordering, and message counts under concurrent producers. With emptiness no
  longer being flush's proxy, the worker loop adopted `Signal::drain()` — the reason it could not
  was this proxy, and the comment that said so is gone with it.
- **`Mat4::inversed()` no longer answers a singular matrix with the identity, silently.** Its one
  production caller caches the result as an instance's world-to-geometry transform, so the old
  fallback would have placed geometry wrongly with no diagnostic at all — the failure mode this
  repository trusts least. A singular matrix now throws `std::runtime_error`, pinned by a test
  that was watched fail against the old fallback first.
- **Device scoring rewarded compute on the graphics family instead of requiring it**, which was a
  real selection bug rather than a cosmetic one. This renderer is nothing but compute dispatches, and
  the constructor aborts on a device that cannot dispatch them — but scoring gave such a device 10,000
  points for being discrete, so it beat a perfectly capable integrated device on 1,110, got selected,
  and killed the process while a working GPU sat unused. Now rejected during scoring, so the loop
  simply passes over it.
- **The ABI promised bit-identical pixels without saying on what.** `docs/PROGRAM_INTERFACE.md` stated
  that "the same camera pose on the same Grid yields bit-identical pixels", and concluded that
  replaying a recorded run must reproduce the same actions. Measured across this machine's two GPUs on
  the same twelve frames: **16.4 % of colour channels differ, the largest by 224 of 255.** The
  guarantee holds per device and does not survive changing one, which matters because it is exactly
  the sort of promise a training pipeline gets built on.

  Cross-device identity is not reachable and is now stated as a non-goal: IEEE-754 pins the four
  arithmetic operations and `sqrt`, but pins neither fused-multiply-add contraction nor how `sin`,
  `cos` and `pow` are implemented, and two vendors' shader compilers choose differently. The target is
  small and measured rather than zero. The replay conclusion survives intact, because replaying
  recorded senses feeds back *pixels*, not poses — so the rule is **record senses, never poses**. And
  the divergence has a use: a Program whose behaviour changes when a pixel moves by two parts in 255
  has learned the graphics card rather than the Grid, which running both devices makes cheap to find
  out.
- **`tools/record_flyby.py` no longer takes a path at all.** `--executable <path>` is replaced by
  `--preset` and `--config`, both checked against constant tuples, so nothing from the command line
  is ever resolved on the filesystem or handed to a subprocess. The build layout is fixed by
  `CMakePresets.json`, so a preset and a configuration say everything a path could — and the flag it
  replaces could name any binary on the machine, which is a sharper tool than recording a flyby
  needs. This is what CodeQL had been objecting to, twice; the objection was right about the shape
  even though the previously-dismissed alerts were correctly dismissed as unexploitable.
- **`tools/record_flyby.py` validated none of its numeric options.** `--frames -5 --render-width 0`
  was accepted, announced as "Rendering -5 frames at 0x720", and then failed inside the renderer as a
  bare exit code — the complaint arriving from the wrong process, about a value the script had held
  all along. Every numeric flag now carries an inclusive range, checked by `argparse` before anything
  is launched, and says what it wanted when it refuses.
- **The recorder preferred the Debug build over Release.** Both produce byte-identical recordings —
  verified — but Debug runs several times slower with the validation layers instrumenting every
  dispatch, so the old ordering merely wasted minutes. Release is now tried first.
- `tools/record_flyby.py` forced the renderer's working directory to the executable's own, justified by
  a comment saying it "loads its compiled shaders from beside itself". It does — from its *executable*
  path, deliberately, so that the working directory does not matter. The workaround outlived its
  reason by some months and is gone.

- **`docs/ARCHITECTURE.md` claimed `SignalsLib::Signal<T>` was "lifetime-safe via `weak_ptr`".** The
  class contains no `weak_ptr` at all — it is a mutex and a `std::queue`. That comment described an
  ownership *convention a caller could adopt*, and neither user in the repository adopts it:
  `LoggingLib::Logger` and the renderer's `RenderChannel` both hold their queue as a plain member and
  outlive it by construction. A design-decision table asserting a safety property that does not exist
  is the kind of thing somebody eventually relies on. Both the table and the header now say what is
  true, and note that the `shared_ptr`/`weak_ptr` arrangement is available and tested but is a
  caller's choice rather than a service.
- **The stated library layout rule was wrong for two of six libraries.** `ARCHITECTURE.md` said each
  has "its own `include/<lib>/` directory"; `signals` publishes `include/signal/` and `logging`
  publishes `include/log/`. Anyone following the rule when adding a library would have produced an
  inconsistent layout, and anyone hunting for a header would have looked in the wrong place.
- **`docs/ACOUSTICS.md` cited "the current TODO item, `Fill hearing_samples`"**, which has not been a
  TODO item for some time. The passage's actual argument — that a flat sample list has nowhere to put
  a second ear — survives and now points at the `TglEarView` per ear that replaced it.
- **`TODO.md` is pruned.** Nine completed etapes collapse to a one-line table; a finished checklist is
  not a plan, and keeping it alongside the changelog and the journal meant maintaining a third copy
  that drifts. The two decisions from them that are still load-bearing — that an arena block is mapped
  once by the arena, and that staging buffers are deliberately not sub-allocated — are called out
  explicitly, and both also live in the code they constrain. The file drops from 721 lines to 577.

- **The acoustic pass never made its histogram visible to the host.** A dispatch wrote it, a fence was
  waited on, and the host read the mapping — but a fence establishes only that the work *finished*, not
  that its writes are available to the host memory domain, and host-coherent memory removes the need to
  invalidate a range rather than the need for the dependency itself. `recordCinematic` has done this
  correctly since Phase 4, forty lines away. Now barriered to `eHost` / `eHostRead`. It happened to work
  on the driver in front of us, which is the least reliable evidence available. (The reverse direction
  needs nothing, and the asymmetry is real: submitting to a queue defines a dependency with prior host
  writes, so the device sees the ear positions without being told.)
- **The ABI documented the impulse response as bin-major; every implementation of it is band-major.**
  `TglEarView::energy` said "bin-major" in both `PROGRAM_INTERFACE.md` and `ACOUSTICS.md`, while
  `Acoustics::ImpulseResponse` and `acoustics.slang` both index `[(band * bin_count) + bin]`. Four bands
  by sixty-four bins does not crash when transposed — it produces plausible nonsense, silently, in
  whichever Program read the documentation. The docs now state band-major, give the index expression,
  and say why: finding arrivals within a band means walking bins, which this layout makes one
  contiguous run.
- **`BvhLib::MAX_DEPTH` and `grid_bvh.slang`'s traversal stack were tied only by a comment**, and the
  failure mode was silent. Raise the host's cap and the builder produces deeper trees while the shader's
  stack stays at thirty — whereupon its `stack_size < MAX_DEPTH` guard *drops* the far child of every
  node it cannot hold, losing hits with no diagnostic anywhere. Now a `static_assert`, verified by
  mutation. The acoustic constants `BIN_SECONDS`, `SPEED_OF_SOUND` and `SURFACE_EPSILON` had the same
  gap and now have the same guard; `BAND_COUNT` and `BIN_COUNT` already did.
- **The fixed-point overflow guard assumed unit amplitude.** It multiplied the ray budget by the scale
  and stopped, but both the source-strength table and the band spectrum are authored by the caller and
  neither is bounded — a spectrum above one would wrap the histogram silently while the check passed.
  Both now enter the product.
- **`docs/ACOUSTICS.md` was an order out on the same arithmetic**, quoting 8,192 deposits for "2,048
  directions by four orders". A ray makes `max_order + 1` segments — the direct one plus one per
  reflection — so the figure is 10,240. The code always computed it correctly.
- Stale claims on the front page and elsewhere: `README.md` said Phase 5 was what "remains" and that the
  embedded clip "is the current output", when it was recorded before the floor gained its terraced
  relief; `docs/ARCHITECTURE.md` still said "when Phase 5 lands"; `src/memory_arena.hpp` described in
  the present tense the sixteen warnings it had just eliminated, and miscounted them as nineteen — that
  was the count of *all* validation warnings, not the sub-allocation ones.

- `docs/ACOUSTICS.md` specified a bump of `TGL_PROGRAM_ABI_VERSION` from `1u` to `2u`. That was
  written before the pre-0.1.0 versioning rule was settled and had been contradicted ever since by
  `docs/PROGRAM_INTERFACE.md` § Versioning, which pins it at `1u`. A design document specifying a
  version bump that the authoritative document forbids is exactly the sort of contradiction that gets
  implemented by whoever reads the wrong one first.
- Documents that still described Phase 5 as planned now describe it as built: the roadmap in
  `TODO.md`, the orientation in `.claude/CLAUDE.md`, `docs/ARCHITECTURE.md` § Acoustic Rays and its
  Material Model section, `docs/VISION.md`, and the doc index in `README.md`.

- **The host and the device disagreed by 1.2 % at one ear, and it was a real bug rather than float
  divergence.** `--verify-acoustics` found it on its first run. Both implementations built their ray
  directions as `golden_angle * ray_index`, which reaches about 4,913 radians — some 782 revolutions
  — at index 2047. A float32 argument that large already carries roughly 6e-4 radians of error, and
  the host and the device then reduce it with *different* algorithms and disagree by about that
  much. At three metres that is two millimetres of displacement, which is enough for a ray to strike
  a two-centimetre neon tube on one and miss it on the other: exactly one ray in 2,048 did, and the
  whole 1.2 % was that single extra arrival in a single bin.

  The diagnosis is worth recording because the obvious explanation was wrong twice. A reflection-order
  sweep showed the disagreement was already fully present at order zero, so it was not bounce
  instability. Nudging the ear by 100 µm moved the host total by 1.6e-5, so nothing was on a knife
  edge — but that test was *too weak* to prove it, since 100 µm only flips rays already within 100 µm
  of an edge. What settled it was running the host's BVH against the host's own brute-force sweep:
  they agreed **exactly**, which acquitted the traversal and left only the direction set.

  Both sides now accumulate the turn *fraction* and reduce before the trigonometry: one multiply, one
  floor, one subtract, one multiply, each of them an operation IEEE-754 specifies exactly, so the two
  produce bit-identical arguments and `cos` only ever sees a value inside one turn. Agreement went
  from 1.2 % to 0.004 %.
- **The fixed-point deposit truncated where it should have rounded.** Losing half a quantum on each
  of tens of thousands of deposits is a systematic deficit rather than noise, and it showed as the
  device reading consistently below the host. Rounding is unbiased and took the remaining
  disagreement from 0.004 % to 0.00005 %.

- **A failed C runtime assertion no longer opens a modal dialog.** On Windows a Debug build's CRT
  reports a failed assertion — including the debug STL's own bounds check on
  `std::vector::operator[]` — by opening a message box and waiting for somebody to click it. In an
  interactive session that is rude; **in CI it is a hang**, because the job has nobody to click the
  box, so a test that would have failed in milliseconds instead sits there until the runner's timeout
  kills it and the log says nothing about which test it was. `TestingLib::runAll` now routes the
  report to stderr, turning it into an ordinary failure with the assertion text and source line
  attached. Guarded on `_DEBUG` as well as `_WIN32`, because outside a debug CRT those calls are
  macros that expand to nothing and the unread loop variable fails the build under `/WX`.
- **An out-of-range material index in the acoustic gather was undefined behaviour.** The source
  table is indexed by a triangle's material with nothing in the type system holding the two to the
  same length, so a short table read past the end of a vector. It is now one compare per hit: an
  undescribed surface is silent and still reflects, which is the only sane reading of a surface
  nobody described. Pinned by a test, and confirmed by mutation — without the guard the process
  aborts on the debug STL's bounds check.
- **`makeMaterials` sized the optical table with a literal `6u`** while the acoustic table used
  `MATERIAL_SLOT_COUNT`. Adding a slot would have grown one table and silently left the other short,
  with the out-of-range read above as the consequence. Both now use the constant.
- Stale attributions in code comments and `static_assert` messages that still credited `trace.slang`
  with declaring `Node` and `Triangle`, and with performing the traversal. `grid_bvh.slang` has owned
  both since the module was extracted.
- The renderer had no DPI awareness, so on any scaled display Windows created a virtual-pixel window
  and DWM upscaled the result — every traced pixel reaching the panel through a bilinear stretch.
- A pointer warp that did not move the pointer swallowed the user's next real mouse movement, and a
  failed pointer grab on X11 was treated as a successful capture.
- Several tests could not fail. The testing library's own self-tests passed with `TEST_CHECK`
  redefined as a no-op, which would have made all ninety-two tests in the repository vacuous; every
  BVH test passed if the builder never split; the quaternion product was only ever tested with
  rotations that commute; and `Mat4::inversed()` — sixteen hand-transcribed cofactors called by
  production code — had no test at all.
- UBSan findings could not fail CI: it defaults to reporting and continuing, so the job went green
  with undefined behaviour in the log. clang-tidy was documented as an error gate and executed by
  nothing.
- Undefined behaviour in the BVH builder: a denormal centroid extent made the bin scale infinite,
  and a centroid at the axis minimum then computed `0 * inf`, casting NaN to `int32_t` inside a
  `std::partition` predicate.
- `lookAt` returned a silently singular matrix whenever the view direction was parallel to up —
  looking straight down being the commonest way to reach it.
- Sixteen abort sites discarded the queued diagnostics that explain them, because `std::abort`
  joins no threads and flushes no streams.
- Glass carried a hard-edged ring where the reflection snapped on. Schlick's approximation is
  defined for the angle measured on the air side of an interface, but on the exit path the tracer
  fed it the shallower angle inside the glass — so reflectance read 0.04 where the true value had
  already climbed past 0.3, and then total internal reflection a degree later snapped it to 1. It
  now uses the angle of the ray that actually leaves, which is the gradual turn to mirror the
  surrounding comment always claimed.
- The final bloom composite sampled the half-resolution mip by integer division, putting a 2x2
  staircase on every neon halo — the exact artefact the downsample pass builds its own bilinear tap
  to avoid. It now samples bilinearly too.
- The bloom pyramid dropped the last column and row at odd resolutions, so a neon tube whose only
  bright pixels sat there contributed no glow. Mip extents round up instead of truncating.
- Objects stood on the wrong floor. `plantOnFloor` asked `gridSurfaceHeight`, which is a step
  function once the relief is terraced, but the mesh that is actually drawn ramps linearly across
  whichever cell a riser passes through — so the two disagree by up to a full terrace step, and the
  glowing column stood 0.29 m clear of its own reflection over a mirror. A new `gridMeshHeight`
  returns the height of the floor *as drawn*, and everything standing in the world asks that
  instead. `src/tests/` now exists to cover it, and the regression test was validated by reverting
  the fix and confirming it fails.
- Neon tubes dipped below the floor on terrace risers. The lift is vertical while the strip widens
  horizontally, so on a slope the outer edge gains no clearance; at the shipped width the steepest
  cell rose further across the half width than the lift allowed. The scene's lift is doubled and the
  header no longer claims a tube "never" z-fights.
- **No release has ever shipped.** The release workflow copied the binary from
  `build/<preset>/Release/`, a directory Ninja Multi-Config never writes — the executable lands in
  `build/<preset>/src/<Config>/`. Every tag push failed both matrix legs before producing anything.
  The archive also omitted the three compiled shaders the binary loads at startup, so even with the
  path corrected a user would have unpacked a program that dies on launch. Both are now moot: the
  build declares `install()` rules and the workflow stages with `cmake --install`, so where the
  artefacts live is knowledge held in one place instead of duplicated as a hand-written path in
  YAML. Release builds now also run the test suite, because a tag can be pushed from any commit and
  the attestation should not vouch for the provenance of an untested binary.
- **The renderer could only be launched from its own output directory.** Shaders were opened by
  bare relative name, which resolves against the working directory rather than the executable's, so
  every IDE debug configuration, every shortcut and every unpacked release failed at startup with
  "Failed to open SPIR-V module: trace.spv". The three paths are now resolved against the
  executable's own location.
- Two design documents claimed surfaces already carry acoustic properties. They do not — the fields
  were reserved once and removed as bloat, `src/components.hpp` says so, and the `static_assert`s
  pin `Material` at 32 bytes. `ARCHITECTURE.md` additionally promised Phase 5 could proceed "without
  touching the layout again", which is exactly backwards: it must.
- `PERCEPTION.md` gave mouse localisation acuity as ~15°. Two independent published groups put it at
  31–33°; the median-plane figure of ~81° is now recorded too, since the document already flagged
  that all its minimum audible angles were azimuthal.
- `PERCEPTION.md` justified coarse acoustic tolerances partly by the precedence effect, which is a
  property of an auditory system rather than of the world — the one thing this repository is not
  allowed to reason about. The tolerances stand unchanged on simulation-side grounds alone.
- The CI cache cleanup never deleted CodeQL overlay-base caches: any key outside the two families
  it knew about became a group of one, and the keep-newest-delete-rest loop cannot fire on a group
  of one. Fifteen dead caches (~260 MB) had accumulated, one per push to main. The cleanup script
  now knows the CodeQL family, reports any unknown family loudly instead of silently keeping it
  forever, and the drifted inline copy of the logic in `ci_main.yml` is gone — both the every-push
  step and the manual workflow now run the same module.
- The swapchain acquire path abandoned the frame on `VK_SUBOPTIMAL_KHR`, leaving the acquire
  semaphore signalled with nothing to consume it and reusing it on the next acquire, which
  VUID-vkAcquireNextImageKHR-semaphore-01779 forbids. Resizing the window triggered it routinely.
- `0 * inf = NaN` in the bounding box slab test made axis-parallel rays launched from exact grid
  coordinates miss boxes they pass through. Both the host reference and the shader are fixed, and
  a regression test covers it.
- The descriptor pool is created with `eFreeDescriptorSet`, which `vk::raii::DescriptorSets`
  requires when it frees its sets at shutdown.
- The surface-area heuristic's leaf guard had its traversal term on the wrong side of the
  comparison and could never fire.
- The bloom upsample derived its source coordinate by integer division, so every texel of a
  destination block sampled the same source position and the glow magnified as visible steps. It
  now samples bilinearly.
- `postprocess.slang` declared its output image format as `unknown`, which requires the optional
  `shaderStorageImageWriteWithoutFormat` device feature. It declares `rgba8` instead, which is what
  the host binds.
- The post-processing stage declared `eBlit` as the destination stage of its final barrier, so a
  consumer that copies rather than blits — which the recording mode does — was unordered against
  the layout transition. It declares `eAllTransfer` now.
- Bloom mip 0 is cleared when bloom is disabled. The tone mapping shader reads it unconditionally,
  and an undefined half float multiplied by zero is only zero if it was not a NaN.

- Two genuine synchronisation defects that Vulkan's synchronisation validation caught, both
  present since Phase 1. The swapchain layout transition claimed `eTopOfPipe` as its source stage
  while the acquire semaphore is waited on at `eColorAttachmentOutput`, so nothing ordered the
  transition after the acquire. And a single depth image was shared by both frames in flight,
  which is a real race rather than a validation nicety — each frame in flight now owns one.
