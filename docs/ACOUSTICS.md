# Acoustics

What changes when the sense is sound rather than light, and the concrete shape of Phase 5 against
the code that exists today.

This document is a design proposal. Nothing in it has been implemented, and every performance figure
below is a **budget** derived from the visual tracer's measured throughput or quoted from somebody
else's published work. No acoustic pass has been profiled, because none exists.

The evidence sits in [research/acoustics.md](research/acoustics.md): the geometrical-acoustics
survey, the creature roster's published audiograms, and the full reference list. This file says what
is being built and why; that one says how we know. Read this one to act, that one to check.

## Scope

TronGrid Lite is the **stage, not the actor**. The acoustic pass ends where the visual pass ends: it
fills a sensor buffer and stops. Everything the surveyed literature does after that point —
head-related transfer functions, vector-base amplitude panning, partitioned convolution,
Doppler-shifted delay lines, Schroeder reverberators, ambisonic encoding — is **auralisation for a
human ear** and belongs nowhere in this repository. There is no 44.1 kHz stream here and there never
will be. That deletes roughly a third of the engineering the literature describes: Schissler and
Manocha's 2017 system spent 75.1 ms of a 258 ms budget on audio rendering alone, and this project
pays none of it.

The same boundary excludes a subtler class of thing. Several of the field's most useful tricks are
models of a *listener* rather than of the Grid — Wwise's abstract diffraction coefficient,
psychoacoustic Doppler thresholds, the precedence effect, the parametric encodings in Project
Acoustics that are defined by what a person notices. Every one of those is cognition wearing a
physics costume. The Grid reports energy arriving at a point in time; whether two arrivals fuse,
whether a reflection is masked, whether a pitch shift is noticeable, is the Program's business and
lives in a Program repository.

Bioacoustics appears here for one reason only, the same reason biology appears in
[PERCEPTION.md](PERCEPTION.md): **to set the size of a buffer.** Frequency range, band count, time
resolution, ear count and ear placement are sensor parameters and are in scope. The moment a question
becomes "how does the animal interpret this", it is out of scope.

PERCEPTION.md has already made this move, and this document inherits the boundary rather than
re-arguing it: its acoustic section names the precedence effect only to exclude it, on the grounds
that the Grid does not get to size its own fidelity on an assumption about how a listener will fuse
what it receives — an assumption there is no reason to extend to a moth with two receptor cells or a
nematode sensing pressure gradients through its skin. Its tolerance conclusions stand on the three
simulation-side mechanisms alone, and so do this document's.

---

## What Changes When the Sense Is Sound

### Propagation delay is the headline

For a renderer, light is instantaneous. A visual ray's answer is a **value**: trace, shade, write a
pixel. Sound travels at about 343 m/s, so an acoustic ray's answer is a **function of time** — an
impulse response — and every consequence in this document flows from that single fact.

The immediate consequence is that the 128 m Grid is 373 ms wide. At the measured 3.7 ms frame time
that is a hundred frames. Sound leaving a source now arrives at a listener over the next hundred
frames, by which time everything that moves has moved.

> **On the frame time.** 3.7 ms is measured at 1280x720 on the reference GTX 1650 Ti in a **Release**
> build with the validation layers off. An earlier revision of this document used 14.4 ms throughout,
> which was measured in a Debug build with GPU-assisted validation active — that instruments every
> buffer access in the traversal loop and inflates the trace pass by roughly 4.6× (16.6 ms against
> 3.4 ms, measured both ways on the same scene). It describes a debugging configuration rather than
> the renderer, and every frame count below has been recomputed.

### The BVH does not have to change at all

This is the happiest finding in the review, and it vindicates the claim
`libs/bvh/include/bvh/bvh.hpp` already makes about itself: *"The same hierarchy is intended to serve
acoustic rays later, which is why nothing here is specific to light."* That holds up on inspection.

`Triangle` is 48 bytes and carries a position, two edges and a material index. `Node` is 32 bytes and
carries an AABB, a child or first-triangle index, and a count. Neither contains a single optical
field. `MAX_DEPTH` of 30 and `MAX_LEAF_TRIANGLES` of 4 are as appropriate for sound as for light, the
slab test is the same slab test, and the stack walk is the same stack walk. **The acoustic pass binds
the same two `VkBuffer` handles with no rebuild, no second structure and no layout change.**

Two existing details in that traversal turn out to matter more acoustically than optically:

- **Back faces are accepted** on both sides, with the comment that "a creature inside a closed shape
  should see its inner surface rather than nothing at all". A creature standing in a terrace hollow
  must hear its walls, so this is now load-bearing rather than defensive.
- **Zero direction components are replaced with `1e-30`** in both `bvh.cpp` and `trace.slang`,
  because `0 * inf = NaN` poisons the min/max reduction. The terraced floor is made of horizontal
  patches, so acoustic rays fired straight up or straight down at a grid coordinate are *more* common
  than in the visual pass, not less. The fix is already in place on both sides.

### The ray payload gains a clock, and the loop condition changes

A visual `RayTask` carries an origin, a direction, a `float3 throughput` and a depth, and dies on
depth or on the `1/512` throughput cutoff. An acoustic task adds one scalar: **accumulated path
length**. It increases monotonically, it is what converts an arrival into a histogram bin index, and
it replaces depth as the primary termination rule — an acoustic ray dies when it has travelled
further than its range cap, which is a different loop condition and a different worst case from a
depth counter.

Everything else about the loop shape carries over. The one live task plus `RayTask stack[MAX_TASKS]`
plus a single `for (segment < MAX_SEGMENTS)` walk is exactly right, and the depth-first split at a
glass surface is exactly the split an acoustically transmissive surface would need if transmission
were ever added.

### The output changes completely, and this is the real work

`traceMain` is one thread, one pixel, one unconditional write:
`output_image[int2(pixel)] = float4(colour, 1.0)`. An acoustic dispatch is thousands of threads
accumulating into **one histogram per ear**. That is a scattered atomic add, and it has no analogue
anywhere in this renderer.

Two constraints bite immediately. `VK_EXT_shader_atomic_float` is not requested, and requesting it
would violate the device policy recorded in the journal — the device asks for nothing beyond Vulkan
1.3 core plus the swapchain extension — so float atomics are off the table. And acoustic energy in an
unbounded response spans many orders of magnitude, so a naive fixed-point scale would either saturate
the direct arrival or quantise the late ones to zero.

The way out has two halves, both described under [The Build Proposal](#the-build-proposal). Give
**one workgroup the whole of one ear**, keep that ear's histogram in shared memory as `uint`s, and
reduce once at the end: sixty-four bins by four bands is 1 KiB, comfortably inside the 16 KiB of
shared memory Vulkan guarantees, and there is no cross-workgroup contention at all. Then observe that
the range cap bounds the dynamic range of the delivered response to about 30 dB, which is what makes
a single fixed-point scale sufficient rather than a compromise. Both halves are stated with their
arithmetic below.

### The direction of the trace, and why the humming neon settles it

The literature is unanimous that acoustics **scatters** — rays leave the source and are collected by
detectors at listeners — and the stated reason is that you cannot backward-trace to a known arrival
time, because arrival time is what you are computing.

That reasoning does not apply to a deterministic specular path. Path length is symmetric, so a
gathered ray that accumulates distance as it goes knows its own delay when it lands. What forward
tracing really buys is a solution to the *receiver* problem: a stochastic ray has vanishing
probability of striking a point, which is why room acoustics needs receiver spheres and why Savioja
and Xiang state the ray-count-versus-detector-size trade so plainly.

The owner's decision that the neon hums inverts this completely. Under
[the hum decision](#the-humming-neon), the sound sources are **16,640 emissive triangles laid along
every grid line of the floor** — a sparse lattice, but one of enormous solid angle from any point
above it, which a gathered ray cannot miss. (Note that the six pillars are `makeEmissive` too and the
glowing column is `makeGlowingGlass`, so 16,724 triangles in this scene carry optical emission. That
is a second reason the acoustic source term must be authored separately rather than read off
`emission`.) Meanwhile a source-driven cast would have to iterate all 16,640 tubes. So the decision
the research argued against is precisely the decision that makes the implementation cheap, and the
reason is already sitting in the shader:

```hlsl
accumulated += current.throughput * material.emission;   // trace.slang
```

Replace `accumulated` with a histogram bin chosen by accumulated delay, and `material.emission` with
the material's authored acoustic source strength times the Grid's hum spectrum, and the acoustic
kernel **is** the visual kernel. Rays find emitters by hitting them, exactly as light does.

Point sources are the opposite case and need the opposite treatment. A creature vocalisation is a
point, a gathered ray will never hit it, and those paths must be enumerated explicitly. Phase 5
therefore has two mechanisms in its *design* — gather for extended sounding geometry, enumerate for
point sources — both walking the same hierarchy. That is not a design smell; it is the two cases
being genuinely different. Only the first is built in the first cut, for reasons given under
[The Build Proposal](#the-build-proposal).

The cost structure that falls out is the **inverse** of the literature's, and the documentation should
say so plainly so that nobody reasons about acoustic cost by analogy with rays per creature. The hum
bed costs `listeners × ears × directions` and is entirely independent of the 8,320 tubes.
Vocalisations cost `listeners × sources × candidate paths`, with a very small constant.

### A snapshot cannot honestly answer a time-domain query

The response is computed against one frame's BVH and arrives over the following tens of milliseconds,
during which the Grid moves. Every interactive acoustics engine has this problem and every one of
them accepts it. The honest thing is to state the bound rather than pretend the snapshot is exact:
**nothing in the delivered response is staler than the response length in creature motion** — about
58 ms, or sixteen frames, because the 20 m cap proposed below bounds *total accumulated path length*,
not one-way range. (The 117 ms out-and-back row in the delay table is what a 20 m monostatic
echolocation range would cost; an emitting creature therefore needs a 128-bin histogram, and that is
a Phase 5b decision.)

### The ABI cannot deliver hearing the way it delivers irradiance

`TglSenses::irradiance` is one float because radiance genuinely is instantaneous. Hearing is not, and
a per-tick scalar would be a lie about the physics. The delay line has to live somewhere, and there
are only two candidates: the Grid or the Program. Propagation delay is physics, not cognition, so
it is the Grid's, and the repository's founding rule settles it without discussion.

A per-tick *scalar* is also too coarse in a second way. At 60 Hz a tick is 16.7 ms, which at 343 m/s
is 5.7 m of path — enough to fold the direct arrival, every terrace bounce and the nearest pillar
into a single number. The resolution that matters is spatial and is stated below: 1 ms is 17.2 cm of
out-and-back range, and a tick is 2.9 m of it. The defensible shape is therefore a short echogram
covering the current tick at 1 ms resolution — sixteen bins by four bands by two ears is 128 floats,
512 bytes per creature per tick, a small fraction of one `insect-min` eye buffer at 1,000–1,500
direction samples, and larger only than `elegans`, whose entire visual sense is two scalars.

---

## What the Literature Settled On

Geometrical acoustics is the right foundation, but only half of its standard architecture applies to
the Grid, which has no ceiling: image sources for early reflections survive, the stochastic
late-reverberant tail does not, because there is no enclosure for a tail to decay in.

That argument, the wavelength ledger it rests on, and why creature scale makes diffraction *less*
important here rather than more, are set out in
[research/acoustics.md](research/acoustics.md#what-the-literature-settled-on-and-which-half-applies-here).

## The Terraced Floor

This is the newest and most concrete part of the Grid, and it invalidates a conclusion several
research lenses reached: that the scene is an open half-space with a flat mirror floor, and therefore
has no echoes worth computing. That was true when they looked. It is now only partly true.

### What the generator actually produces

`gridSurfaceHeight` in `src/geometry.cpp` sums three octaves of smoothstep value noise at a base
wavelength of 46 m, normalises to `[0, 1]`, quantises with `floor(relief * 6) / 6`, and scales by a
5 m amplitude. The scene in `main.cpp` uses 64 cells at 2.0 m, so a 128 m floor of 8,192 triangles,
unchanged in count from the flat version — the relief displaces vertices that already existed.

Evaluating that function over the shipped landscape (seed 42) gives hard numbers rather than
adjectives:

| Measured over the built floor | Value |
|-------------------------------|-------|
| Distinct terrace levels | 6, at 0, 0.833, 1.667, 2.500, 3.333 and 4.167 m |
| Terrace step | exactly 0.833 m everywhere; no double steps occur |
| Grid edges crossing a level change | 693 of 8,320 (8.3 %) |
| Floor triangles that are dead flat | 7,014 of 8,192 (85.6 %) |
| Riser triangles at 22.6° | 990 |
| Riser triangles at 30.5° (a step crossing a quad diagonally) | 188 |
| Steepest facet anywhere | 30.5° |
| Terrace run along a grid axis | median 7 cells (14 m); over both axes, mean 10.3 cells (20.5 m), longest 62 cells (124 m) |
| Total riser surface | ≈ 2,581 m², about 15.5 % of the floor |

Two notes on that table. The top terrace: `floor(relief * 6) / 6` yields levels `k/6` for `k` in 0 to
5, so the highest ground stands at five sixths of the amplitude — **4.17 m, not 5 m**. The amplitude
is the parameter; the top level is the consequence. And the run statistics are direction-dependent in
a way it is easy to quote wrongly: along X the mean is 9.7 cells and the longest run 50 cells, along
Z the mean is 11.0 cells and the longest 62; the median is 7 cells along X and 8 along Z. The
combined figures above are the ones used everywhere else in this document.

**What stands on the floor is partly inside it.** `plantOnFloor` samples `gridSurfaceHeight` at a
box's centre and at the four corners of its footprint and takes the **minimum**, so a pillar or slab
straddling a level change is set into the higher ground rather than left hovering over the lower. The
acoustic consequence is not a slot underneath — there is none — but that the buried portion of a
face is not a reflector at all. The image-source enumeration below counts "about fifty outward-facing
box faces", and for a box straddling a step the usable extent of the two faces on the high side is
reduced by up to 0.833 m at the bottom. The validation ray catches this for free, because a
reflection point below ground fails the ray test like any other; it is recorded here so that nobody
reads "fifty faces" as "fifty full rectangles".

### What it does acoustically

Four mechanisms, in descending order of how much they matter.

**Multiple floor images, at resolvable delays.** A flat plane gives a listener exactly one floor
reflection and one image source. Six terrace levels give one image per level in line of sight, at
different heights and therefore different delays. Two adjacent terraces differ by 0.833 m, which is
2.43 ms of one-way path and about 4.9 ms of round-trip difference for a near-vertical bounce. At 1 ms
bins those land in different bins. This is real, new, cheap to compute and exactly what a heightfield
buys.

**Occlusion that actually exists.** A flat plane cannot occlude anything. A 0.833 m step is a genuine
barrier to a creature five centimetres tall, and — this is the point of the wavelength ledger — it is
7.3 wavelengths across at the 3 kHz hum, 5.6 at the mouse's 2.3 kHz floor and 208 at its 85.5 kHz
ceiling. The ray model's shadow behind a terrace step is physically defensible across the whole
roster's hearing range, which is more than can be said for the neon.

Put a number on how wrong it still is. Take a creature at 5 cm behind a 0.833 m step with a source at
5 cm height 5 m in front of it, listener 2 m behind. The blocked direct path is 7.00 m; the path over
the edge is 7.209 m; the detour `δ` is 0.209 m, so the Fresnel number `N = 2δ/λ` is 0.12 at 100 Hz,
3.7 at 3 kHz, 9.8 at 8 kHz, 37 at 30 kHz and 104 at 85.5 kHz. Maekawa's empirical curve gives about
**5 dB of attenuation at `N = 0`, not silence**, rising roughly logarithmically thereafter. So at the
hum fundamental the ray tracer's hard zero overstates a real but finite shadow; by the second
harmonic and everywhere the mouse hears well, the barrier is working hard and the ray answer is wrong
by a bounded and shrinking amount. Menounou's correction is worth knowing about precisely because it
addresses the two regimes where even the empirical curve misbehaves — receivers close to the barrier
and receivers near the shadow boundary — which are the two regimes a small creature walking past a
terrace step lives in permanently.

**Deflection instead of escape.** A facet tilted by `θ` rotates the specular direction by `2θ`. At
22.6° that is 45.2°, at 30.5° it is 61°. A ray skimming a flat plane at grazing incidence bounces
forward at grazing and keeps skimming until it runs off the edge; over terraced ground it is kicked
well off the mirror direction, sometimes up out of the Grid and sometimes down into the next
terrace. That is geometric scattering, modelled as geometry.

**Concave corners, which do almost nothing.** Where a riser meets the terrace below it the dihedral
is about 157°; the junction with the terrace above is convex, at about 203° through the air, and does
not even qualify. Retro-reflecting dihedrals want 90°. This mechanism is not doing useful work and
should not be claimed.

### What it does not do: the risers are not vertical

The design comment in `src/geometry.cpp` describes "near-vertical risers between them". The generator
cannot produce those, and the reason is arithmetic rather than a bug.

`generateGridFloor` is a heightfield sampled at the cell corners. A level change between two adjacent
samples 2 m apart becomes one quad rising 0.833 m over 2 m of run — **22.6°**, or 30.5° where the
step crosses a quad diagonally. The mesh has no way to represent anything steeper, because the
shortest horizontal distance it can express is one cell.

That matters, and it matters most for the one capability the acoustic phase makes possible for free.
Consider a monostatic echolocator: source and listener co-located, listening for its own echo. In a
perfectly specular world only facets whose normal points back at the emitter return anything. A
horizontal ray from a creature at 5 cm hitting a 22.6° riser reflects **upward at 45° and never
returns**. Retro-reflection off a 22.6° facet requires the ray to arrive along the facet normal, i.e.
22.6° from vertical, which for a creature at height `h` means a riser at a horizontal distance of only
`0.42 h` — two centimetres for a five-centimetre creature. **As built, the terraces cannot support
monostatic echolocation at all.**

With genuinely vertical risers they can, immediately and at any range: a creature emitting
horizontally hits a vertical face square on and the echo comes straight back. That is the behaviour
the design comment reasons about — *"a riser standing square to the ground throws sound back across
the Grid"* — and it is one generator change away.

**Recommendation.** Have `generateGridFloor` emit an explicit vertical quad at every level change
rather than tilting the surface quad. Cost: 693 boundary edges, two triangles each, **1,386 extra
triangles** — the floor goes from 8,192 to 9,578 and the scene from 24,952 to 26,338, an increase of
5.6 %. In exchange the Grid gains **1,155 m² of genuinely vertical wall** distributed across 128 m,
and every one of those square metres retro-reflects. The alternative, dropping `cell_size` well below
the terrace step, is worse: at 0.25 m the risers reach 73° but the floor costs 524,288 triangles.

If neither change is made, the honest statement in the documentation must be that the relief tilts
reflections rather than returning them, and that echolocation on the Grid is bistatic only.

### What the relief changes in the research's conclusions

Two conclusions have to be revised and one has to be defended.

**Revised: "the scene has no echoes."** It does now. A listener standing mid-terrace has steps roughly
7 m away in either direction (half the median run). Under the hum bed those risers are emitters in
their own right, so they contribute one-way arrivals around 20 ms, and a bistatic path from a source
beyond a step arrives around 41 ms. Six terrace levels give a family of floor images 2.4 ms apart in
one-way path. That is a sparse, structured, learnable set of arrivals in the 20–120 ms band which a
flat plane simply did not produce. What does not arrive is the monostatic echo: a 22.6° riser
deflects rather than returns, which is what the vertical-riser recommendation above is for.

**Revised: the ~55-plane image-source enumeration.** The recommendation to enumerate image sources
over "one floor plane plus about fifty box faces" assumed a flat floor. There is no single floor plane
any more. The distinct acoustic planes are now six terrace levels plus 1,178 riser facets plus about
fifty outward box faces — roughly 1,234 first-order candidates, and about 1.5 million at order two.
Pure image-source enumeration over the floor is dead. What survives is a hybrid: enumerate the **six
terrace levels** (treating each as an infinite horizontal plane, reflecting the source in it, and
accepting the candidate only when a validation ray confirms the reflection point lies on a triangle at
that level) and the **fifty box faces**; gather everything else.

**Defended: there is still no reverberation.** The relief adds surface, not enclosure. There is no
ceiling, there are no walls, and the sky is still infinite and black. No heightfield facet can turn an
upward-going ray back down: its outward normal always has a positive vertical component, so a
reflection off its visible side makes an upward ray steeper rather than shallower. That holds at 22.6°
as built and equally at 90° with the vertical risers recommended above, so energy leaving with an
upward component is gone forever either way, and the riser change does not put this conclusion at
risk. Rays still escape after one or two bounces. RT60 is still undefined, Sabine's formula still has
an unbounded volume, and a statistical reverberator would still be synthesising a signal the physics
does not produce.

### Echo and reverberation are not the same thing

The distinction is worth stating precisely, because the relief makes one of them real without making
the other real.

A **discrete echo** is a single late arrival from one large surface. It occupies its own bin in the
histogram, it has a definite direction and a definite delay, and a listener can in principle attribute
it to a place. This scene produces those, and the terraces were shaped to produce them.

**Reverberation** is a dense decaying tail from an enclosure, in which arrivals become too numerous to
resolve individually and only their statistics carry information — decay rate, spectral tilt, and the
sense of a room's size. It requires energy to circulate rather than leave. This scene cannot produce
it, and RT60 measures precisely this quantity, which is why quoting a reverberation time here would be
a category error rather than an approximation.

The practical consequence for a creature is worth being blunt about: **hearing on the Grid is a
sense of where things are, not a sense of what kind of space this is, because there is no space.**
Direction, distance, and occlusion shadows behind terrace steps and pillars are real information a
Program can use. A sense of enclosure is not available, and no better algorithm would make it available.
If it is ever wanted, the fix is Grid geometry, and that is a VISION.md decision rather than an
acoustics one.

### Delays this geometry actually produces

At 343 m/s, with the frame column at the measured 3.7 ms:

| Path | Delay | Frames |
|------|-------|--------|
| One histogram bin (1 ms) | — | 34.3 cm of path, 17.2 cm of out-and-back range |
| Terrace step, one way | 2.43 ms | — |
| Grid cell | 5.83 ms | — |
| Room-acoustics bin convention (4 ms) | — | 1.37 m of path |
| Glass slab width | 17.5 ms | 4.7 |
| Median terrace run (14 m) | 40.8 ms | 11.0 |
| Mean terrace run (20.5 m) | 59.8 ms | 16.2 |
| Nearest pillar separation (~25 m) | 72.9 ms | 19.7 |
| Proposed 20 m range cap, out and back | 117 ms | 31.6 |
| Floor half-extent (64 m) | 187 ms | 50.4 |
| Floor width (128 m) | 373 ms | 100.8 |
| Floor diagonal (181 m) | 528 ms | 142.7 |

The 17.2 cm figure is the cleanest way to communicate the time resolution, and it comes from
Schnitzler and Kalko's observation that each millisecond of signal duration adds 17 cm to a bat's
minimum detection distance. **The bin width is a distance resolution.** Choosing 1 ms bins means
resolving arrivals 17 cm apart in range, which is a creature-scale number; choosing the
room-acoustics 4 ms means 69 cm, which is not.

---

## The Acoustic Material Model

Two research lenses reached opposite conclusions and both argued them well. The bioacoustics lens,
following Steam Audio's shipping `IPLMaterial`, concluded that a material should carry **seven
floats**: three absorption coefficients, one scattering scalar, three transmission coefficients. The
materials lens concluded that the honest minimum is **one authored float** — a broadband absorption
coefficient — with a scattering coefficient alongside it defaulted to zero.

### The resolution: one authored float for the surface, one for the source, and no scattering at all

This document goes further than either lens in one direction and one step back in another.
**`acoustic_absorption`, one broadband float per material, is the whole of the *surface response*
model.** No bands, no scattering, no transmission.

One further float is unavoidable and is not a surface response at all: **`acoustic_source_strength`,
the acoustic counterpart of `emission`**, because the neon hums and because optical emission is the
wrong selector — the six pillars are `makeEmissive` and the glowing column is `makeGlowingGlass`, and
neither is meant to sing. A gather keyed on `material.emission` would find 16,724 triangles where the
design calls for 16,640. So: **two floats per material, twelve floats, 48 bytes for the entire
acoustic material table.**

The argument for keeping it at two is the same argument `components.hpp` already makes for the optical
model, and it is worth stating in the same shape. The optical model is not a simplification of
physically-based rendering; it is PBR at the smooth limit, where the microfacet distribution collapses
to a delta function and the BRDF reduces analytically to Fresnel-weighted mirror reflection plus Snell
refraction. **The acoustic model should have the same character: not a cheap approximation, but the
correct closed form for the geometry the Grid actually contains.**

#### Why the source term is a scalar and not a spectrum

The hum has one spectrum — a 3 kHz fundamental and its harmonics, settled under
[the hum decision](#the-humming-neon) — and every tube on the Grid radiates it. What differs between
one sounding material and another is *how loudly*, not *with what colour*. So the spectrum is a single
Grid-level constant, one `float4` over the listener's bands, and the material carries the scalar that
multiplies it. That is the same split the next subsection argues for absorption, arriving from the
other side: the band structure lives at the deposit, and the material carries one number.

It also keeps the door open honestly. A creature vocalisation is a point source enumerated separately,
carries its own spectrum in its own descriptor, and never touches the material table. If a second
*surface* spectrum is ever wanted — a machine that whines where the tubes hum — that is the concrete
second use case, and a `float4` per material is what it costs. Not before.

#### Why not per-band absorption

Frequency dependence is real for porous and resonant absorbers and negligible for everything on the
Grid. The pyroomacoustics materials database gives smooth unpainted concrete `0.01 … 0.05` across
125 Hz to 8 kHz, rendered brickwork `0.01 … 0.04`, 3 mm glass `0.08 … 0.02`. Converted to what a
renderer multiplies by, those are losses of **under 0.3 dB per bounce**, and under about 3 dB after
ten bounces, most of it concentrated in the 125 Hz band where the geometry is invalid anyway. The one
material in that table that genuinely needs bands is carpet, which swings by a factor of eleven. There
is no carpet here, no drape, no porous absorber, and by design there never will be.

The decisive argument is structural rather than numerical. Write the energy delivered along one path
in band `b`:

```text
E_b = S_b * prod_i (1 - alpha_i) * (1 / (4 * pi * d^2)) * 10^(-a_b * d / 10)
```

If `alpha` is band-independent, the bounce product and the spreading term are shared by every band, and
**the only band-dependent factors are the source spectrum `S_b`, applied once at emission, and the air
term `a_b`, which depends only on the total path length `d`.** Both can be applied at the moment the
arrival is written into the histogram. So a single broadband ray, carrying one throughput scalar and
one accumulated path length, populates an N-band histogram **exactly** — not approximately — at a cost
of N multiplies per recorded arrival.

That has a second payoff worth naming, because it settles an architectural question the roster raises.
The presets do not share band edges: `elegans`, `rodent` and `macropod` have three disjoint sets. With
band-independent absorption the *trace* is band-agnostic, so the same gather serves any band set, and
the band edges are applied only where the arrival is deposited. One solve per ear, each ear using its
own edges, is therefore both the biologically correct answer and the cheap one — which is exactly what
the budget table assumes.

Make `alpha` banded and both collapse: the product differs per band, must be carried down the ray, the
payload becomes N floats with N multiplies per bounce, and a ray becomes specific to one listener's
band edges. In this renderer that is the expensive direction, and it is expensive for a reason the
repository has already measured. The sweep table in `trace.slang` shows the task stack rising from 3
to 6 costing 15.24 ms to 19.00 ms at depth six, with the note that the two axes "cost roughly a
doubling across the range, and they multiply rather than add". Register-resident per-thread state is
the measured cost driver here. **Bands belong in the histogram, not in the material and not in the
ray.** That decision is also reversible without touching the trace or the material buffer, which is
exactly the kind of decision this repository likes to keep cheap.

#### Why not a scattering coefficient

This is where this document departs from both lenses, and the terraced relief is what makes the
departure clean.

The scattering coefficient `s`, standardised in ISO 17497-1, is the proportion of reflected energy
that does *not* leave in the specular direction. (It is routinely confused with the diffusion
coefficient of ISO 17497-2, which measures the uniformity of the reflected polar distribution; Cox and
colleagues wrote a fifteen-page tutorial because practitioners kept swapping them. A renderer wants
`s` and never `d`.) In practice `s` is a **statistical stand-in for geometry the model does not
carry** — the coffering, seating and mouldings a room model omits, plus, in packages like Odeon, an
explicit allowance for edge diffraction from finite panels.

Four reasons it does not belong here:

1. **There is no unmodelled geometry for it to stand in for.** Every surface on the Grid is a
   mathematically planar quad, and that quad *is* the geometry. Acoustic smoothness is judged against
   wavelengths of centimetres to metres, so these surfaces are acoustically smooth at every frequency
   any creature on the roster can hear. Zeng, Christensen and Rindel recommend 0.005–0.02 for smooth
   painted concrete and 0.02–0.05 for smooth surfaces generally — and note that even those small
   numbers exist mostly as a proxy for edge effects rather than for roughness. A field whose only
   correct value is its default is the definition of the thing `components.hpp` already refuses:
   *"a field nothing reads is a field nothing maintains."*
2. **The terraced relief is the owner's own answer to the same problem, and it is a better one.** The
   complaint that motivated the relief — a flat mirror plane sends every reflection away and none
   returns — is precisely the complaint `s` exists to paper over in room acoustics. The response was
   to change the geometry, not to add a parameter. That instinct is right and it is the same instinct
   MATERIALS.md follows. Scattering now lives in the mesh, where it can be inspected, tested against a
   brute-force reference, and seen in the picture, rather than in a float that can only be tuned by
   ear.
3. **The remaining thing `s` conventionally absorbs is edge diffraction, which this renderer explicitly
   does not model.** Dressing an unmodelled wave effect up as a "scattering" parameter would be a fudge
   factor with no measurable value and no testable effect — the opposite of how every other number in
   this repository is chosen.
4. **Using it would cost the determinism guarantee.** The energy balance
   `(1 - s)(1 - alpha) + alpha + s(1 - alpha) = 1` splits a reflection into a specular part and a
   diffuse part, and the only implementation compatible with one-ray-per-bounce is the stochastic one
   — Russian roulette between `reflect(I, N)` and a cosine-weighted random direction. There is no RNG
   anywhere in this repository, and PROGRAM_INTERFACE.md publishes bit-identical replay as a guarantee.
   Introducing the project's first Monte Carlo estimator, and the temporal accumulation that would
   follow it, to model a phenomenon whose correct coefficient here is zero, is a bad trade.

The counter-evidence deserves an answer rather than an omission. Rindel reports an international round
robin in which, of sixteen simulation programs, only three gave unquestionably reliable results, and
observes that the best programs used some kind of diffuse reflection while purely specular models were
the outliers. That finding is about **reverberant tails in enclosures**, which this scene does not have
and cannot have. The failure mode it documents — a purely specular model producing an unrealistically
slow, flutter-ridden decay — requires many bounces to develop, and rays here escape after one or two.
The warning does not transfer, and saying so explicitly is better than either ignoring it or importing
a parameter to placate it.

The cost of this decision is real and named in the previous section: a purely specular world
backscatters only from facets whose normals point at the emitter, which is why monostatic echolocation
needs the risers to be vertical. **If the specular-only acoustic model ever proves too impoverished,
the correct fix is more geometry, not a statistical float.** That is the same answer the relief already
gave once.

#### Why not transmission

The mass law gives a 4 mm float glass pane at 10 kg/m² a transmission loss of about 33 dB at 1 kHz, so
a transmitted energy fraction near `5 × 10⁻⁴` against a reflected fraction near 0.97 — three orders of
magnitude down. It only ever matters when the direct path *and* every reflected path are simultaneously
blocked, which on an open floor with strong terrace bounces essentially cannot happen. Even Steam
Audio, which does carry a three-band transmission array, documents it as "only used for direct
occlusion calculations" and never carries it through the reflection tree. Defer it, and if it is ever
added, follow that precedent exactly.

There is a pleasing inversion worth recording. Optically, glass is the most interesting material on
the Grid: transmission towards 1, Snell refraction, total internal reflection, the whole apparatus of
[MATERIALS.md](MATERIALS.md). Acoustically the same slab is a near-perfect mirror with `alpha ≈ 0.03`
and `tau ≈ 0.0005`. **The one surface the optical renderer treats as transparent is the one the
acoustic renderer treats as most opaque** — which means a creature with both senses gets information
neither alone provides. That is "one Grid, two senses" earning its keep rather than merely being
asserted.

#### There is no acoustic Fresnel, and this is the genuine asymmetry

The optical model authors one number, the index of refraction, and derives both `F0 = ((n-1)/(n+1))²`
and the refraction direction from it. The acoustic version of that derivation collapses. Air's
characteristic impedance is about 415 rayl; glass is around `13 × 10⁶` rayl, a mismatch of some 30,000
to 1. Feeding those into `R = (Z2 - Z1)/(Z2 + Z1)` gives `|R| = 0.99994` and `alpha` of order
`1.3 × 10⁻⁴` — a perfect acoustic mirror. The measured coefficient for a 3 mm pane is 0.02 to 0.08,
two to three orders of magnitude higher, because real absorption at a hard surface comes from panel
flexure, resonance, mounting and edge losses that a bulk impedance model knows nothing about.

So absorption must be **authored**, not derived. It is the one acoustic parameter with no optical
counterpart, and any attempt to compute it from a "sonic IOR" would produce a world in which nothing
ever decays. The documentation should say this plainly rather than pretending the symmetry between the
two senses is complete. The source strength is authored for the same reason and one further one: there
is no reference level on the Grid at all, so the number is relative by construction.

#### Values, and how uncertain they are

| Material | `acoustic_absorption` | `acoustic_source_strength` | Basis |
|----------|----------------------|----------------------------|-------|
| Floor | 0.02 | 0 | Polished hard surface; the most consequential number in the model, because every path touches it |
| Pillar | 0.03 | 0 | Hard surfaces, average. Optically emissive, acoustically silent — the whole reason the source term is authored separately |
| Glass slab | 0.03 | 0 | 3 mm glass, mid-band |
| Glowing glass column | 0.03 | 0 | As the slabs; likewise emissive and likewise silent |
| Neon primary | 0.02 | 1.0 | Glass envelope. The unit of the scale: every other strength is relative to a primary tube |
| Neon accent | 0.02 | 1.0 | Identical hardware to the primary; the two differ in gas colour, which is an optical property and not an acoustic one |

Six materials, two floats each: **48 bytes for the entire acoustic material model.**

Two honesty notes belong beside those numbers. Treble's documentation states that measured absorption
coefficients "can have errors of about ±0.2" — for a hard surface at 0.03 that is a nominal uncertainty
several times the value itself. Vorländer's 2013 review concludes that predicting reverberation times
better than the just-noticeable difference "requires input data in a quality which is not available
from reverberation room measurements". **The input data are worse than the algorithms**, and that is a
licence to keep the model small, not a defect to engineer around. MATERIALS.md accepts Schlick's
approximation on a 1 % error budget; the acoustic equivalent is roughly twenty times more permissive.

Put the decay in proportion while the numbers are in hand. At `alpha = 0.02`, ten bounces cost
`0.98¹⁰ = 0.817`, which is **0.88 dB**. Over the same 128 m, spherical spreading costs about 42 dB from
a one-metre reference and air absorption at 8 kHz costs 9.8 dB. **Surface absorption is the smallest
term in the entire model by an order of magnitude**, which is the final reason one float carries it.

### Where it lives: a parallel buffer, not a third row on `Material`

`Material` is exactly 32 bytes, `alignas(16)`, two full std430 rows with no padding anywhere, guarded
by six `static_assert`s in `components.hpp` and re-asserted independently in `tracer.cpp`. Because it
is 16-byte aligned, adding even one float costs a whole row: 32 to 48 bytes, a 50 % increase, plus
edits to four declarations that must all move in one commit — the struct, its asserts, the Slang mirror
in `trace.slang`, and the cross-check in `tracer.cpp`.

The failure mode if one is missed is **asymmetric, and only one direction is caught.** The July 2026
bug was the shader declaring a *wider* `Material` than the host, which strides past the end of the
buffer and which GPU-assisted validation reported. The reverse — host wider than shader — silently
reads the wrong material for every triangle, with no validation error and no assert that fires.
Nothing in the repository guards that direction.

**Recommendation: put the two acoustic floats in a separate `StructuredBuffer<float2>` indexed by the
same material index, bound only by the acoustic pass.** `Material` stays 32 bytes, its asserts stay
untouched, `trace.slang` is not modified at all, the visual pass never binds the acoustic table, and
the buffer is twelve floats. The silent-failure window never opens.

This does brush against design principle 5 as README.md currently words it — "surfaces carry optical
and acoustic properties together" — and that wording should be adjusted rather than the design bent to
fit it. The principle that matters is that this is **one Grid**: one BVH, one triangle table, one
material index answering both senses. Whether the two coefficient sets share a cache line is an
implementation detail, and coupling them by index rather than by row is the honest reading. It is also,
exactly, what the de-bloating pass concluded when it deleted these fields the first time.

---

## What a Creature Ear Needs

### The roster

The six presets and the published audiograms behind them are tabulated in
[research/acoustics.md](research/acoustics.md#the-creature-roster). What matters for the sensor
shape below is the span they collectively cover, and the fact that no single band is audible to all
of them — which is what the band choice has to solve.

### Bands: four, chosen from the audiogram

Offline room acoustics uses seven or eight octave bands from 62.5 Hz or 125 Hz to 8 kHz — the ISO 266
preferred series, with ISO 3382-1 requiring at least 125 Hz to 4 kHz and pygsound carrying eight. Every
shipping real-time system uses far fewer. Steam Audio uses **three**: low up to 800 Hz, mid 800 Hz to
8 kHz, high above 8 kHz. Schissler, Mehra and Manocha use four; Schissler and Manocha use four
logarithmic bands and are explicit that the reason is 4-wide SIMD.

Four is right here, for the same engineering reason expressed differently — **a band vector fits one
`float4` in Slang** — and it is generous against the biology, which tops out at one band for a moth and
no frequency discrimination at all for a nematode.

The band *edges* must come from the audiogram rather than from architectural convention, and they
differ per preset. This is the acoustic analogue of the per-eye sample-direction list `TglEyeDesc`
already carries, so it needs no new pattern:

| Preset | Band edges |
|--------|-----------|
| `elegans` | 0.1 – 5 kHz (one band) |
| `rodent` | 2.3 – 5, 5 – 11, 11 – 32, 32 – 85.5 kHz |
| `macropod` | 0.5 – 2, 2 – 6, 6 – 14, 14 – 40 kHz |

Only the `rodent` bands sit wholly in the regime where rays are valid. The `macropod`'s lowest band
straddles the 500 Hz to 1 kHz boundary — and 0.5 kHz and 2 kHz are ISO 266 octave centres, so the claim
that no edge coincides with convention would be false as well. The single `elegans` band reaches down
to 100 Hz, where the ray answer for everything but the terrace levels is fiction. That is not a reason
to move the edges — they come from the audiogram, not from convention — but it must be said plainly
that below about 500 Hz the Grid delivers `elegans` an energy figure whose spatial structure it cannot
vouch for.

This is the one correction to PERCEPTION.md this document proposes, and it must be made in two places:
the acoustic section, which quotes "6–9 octave bands" as the real-time tolerance, and binding rule 10,
which repeats it and also prescribes "per-block interpolation" that a repository with no waveform never
performs. The offline figure is right for offline work; the shipping systems are tighter, and the
reason is an engineering one this project shares exactly.

### Time bins: 1 ms, not 4 ms

The canonical worked examples use 4 ms — MathWorks' Audio Toolbox tutorial and pyroomacoustics both
default to `0.004` s. That convention exists to keep a **stochastic tail** smooth with a modest ray
budget, and this scene has no tail. What it has instead is a handful of resolvable arrivals, and the
resolution that matters is spatial: 1 ms is 17.2 cm of out-and-back range, which is a creature-scale
number; 4 ms is 69 cm, which is not.

The cost of the finer bin is four times the bins, and the bins are the cheapest thing in the design. A
64-bin, four-band, two-ear histogram is 512 floats; the slice delivered per tick is 128 floats.

### Two ears, never a mono mix

The Grid can make localisation *possible*; it cannot and must not perform it. What it has to deliver
is the physical basis: two ears at a stated separation, each receiving its own signal, so that
arrival-time and level differences exist in the data. Heffner and Heffner enumerate the three cues —
binaural time difference, binaural intensity difference (which requires the head to cast a shadow,
hence the small-head/high-frequency coupling), and the monaural pinna cue that resolves front-back and
elevation and needs a complex spectrum rather than a pure tone.

For small bodies the magnitudes are brutal, and they are a warning rather than a specification. Römer
gives a 3 mm grasshopper facing a 7 cm wavelength an interaural time difference of 3 µs. Miles, Robert
and Hoy measured *Ormia ochracea*'s ears about 520 µm apart with arrival-time differences of less than
1 to 2 microseconds — and the fly solves it **mechanically**, with a cuticular bridge coupling the two
tympana, not neurally. Many insects use pressure-difference receivers in which sound reaches both faces
of the tympanum, making the ear itself intrinsically directional.

Two things follow. Interaural time differences of microseconds cannot survive 1 ms bins, so if binaural
timing is ever wanted the histogram must carry a direction per arrival rather than energy alone — a
different data structure, cheap in a scene with few arrivals, and deliberately not proposed for the
first cut. And the *Grid* should stop at two buffers: how a Program extracts direction from them is
the Program's problem, and *Ormia* is a standing warning that the mechanism may live in the body's
mechanics rather than in computation at all.

The current TODO item, "Fill `hearing_samples` in the Program interface", names a flat array. **A flat
sample list is precisely the shape that makes localisation impossible**, and the name should be changed
before it freezes. Worth stating plainly: no such field exists anywhere in the repository, and
`tgl_program_abi.h` does not exist as a file at all — the ABI lives only as C snippets inside
[PROGRAM_INTERFACE.md](PROGRAM_INTERFACE.md). That makes this the cheapest possible moment to shape it.

### Solve rate: two rates, for two different things

PERCEPTION.md records that a moving-source auralisation matched offline quality at an 8.6 Hz average
solve rate, and concludes that "the acoustic solve can run once per seven to ten frames and remain
transparent". Schäfer, Fatela and Vorländer's figures are right — a 10 Hz maximum simulation rate,
8.6 Hz achieved, about 116 ms between solves, against audio at 44,100 Hz in 1,024-sample blocks. Steam
Audio ships the same ratio as a default: `SimulationUpdateInterval = 0.1f`.

That conclusion is sound for a passive listener and **wrong by a factor of about twenty for an
echolocating emitter**. Schnitzler and Kalko document the terminal buzz as "a series of short signals at
a high repetition rate (up to 180–200 Hz)", with a single bat's demand on the Grid spanning two orders
of magnitude between cruising and the final hundred milliseconds before capture — and the transition
driven by the animal's own behaviour, which the Grid cannot schedule.

The reconciliation is that these are two different solves. The **ambient solve** — the hum bed and the
Grid's steady reflections — can run at 10–20 Hz because the geometry changes slowly, and the buffer
can simply be held between solves. (The interpolation Schäfer and colleagues needed is interpolation of
a *waveform*, and there is no waveform here, so this repository pays none of that cost either.) A
**vocalisation** is not an ambient solve; it is one impulsive event, 1–3 ms long, whose response is one
enumeration against a 20 m range cap. At 200 pings per second that is a few tens of thousands of
ray-segments, which is nothing. **The Grid must schedule per source rather than picking one global
rate**, and that is an architectural fact rather than a tuning parameter.

Two other numbers from the same source bound the trace usefully. A bat emitting at 112 dB SPL detects a
2.5 cm fluttering insect at "not more than 10.5 m", and their conclusion is unambiguous: "echolocation
is a system that works only over short distances". And signal duration imposes a *minimum* usable
distance through self-masking, at 17 cm per millisecond of call — which is the same 17 cm that sets the
histogram bin.

### The ABI shape

Following the descriptor/view pattern that vision already uses, and the ABI's own rules — C99,
fixed-width types only, no hand-written padding, no ownership transfer, layout selectors as `uint32_t`
constants rather than enums:

```c
/*! Geometry of one ear. Fixed for the creature's lifetime. */
typedef struct TglEarDesc
{
    float position[3];        /*!< Ear position in body frame, metres. */
    float direction[3];       /*!< Ear axis in body frame, unit length. */

    /*! Band edges in hertz, band_count + 1 values, ascending. Borrowed for the call only.
        Chosen from this body's audiogram, not from room-acoustics convention. */
    const float* band_edges_hz;
    uint32_t band_count;

    uint32_t bin_count;       /*!< Time bins delivered per tick. */
    float bin_seconds;        /*!< Width of one bin. */
} TglEarDesc;

/*! One ear's arrivals for this tick. */
typedef struct TglEarView
{
    /*! Energy per (bin, band), bin-major, bin_count * band_count floats. Never NULL.
        Relative to the emitting source's energy; there is no absolute reference level.
        A bin no sound has reached yet reads zero, which is the physical answer and not a
        sentinel: there is no "not yet filled" state to flag. */
    const float* energy;

    uint32_t bin_count;       /*!< Repeated from TglEarDesc so a tick handler needs nothing else. */
    uint32_t band_count;
} TglEarView;
```

`TglCreatureDesc` gains `const TglEarDesc* ears; uint32_t ear_count;`. `TglSenses` gains
`const TglEarView* ears; uint32_t ear_count;` under a new `/* -- Hearing -- */` banner, keeping the
modality grouping the document commits to. `ear_count` of zero is a legitimate body and is the correct
specification for all three insect presets.

The direction of control is the same as for eyes: **the Grid decides how many ears a body has, where
they sit, and what bands they resolve.** A Program does not request an ear.

If echolocation is wanted — and PROGRAM_INTERFACE.md already observes that it "needs no new sense at all"
once hearing and vocalisation exist — `TglActions` gains a vocalisation field in the **same** bump, not
a later one. That is the whole of the second breaking change, and it should not be split.

`TGL_PROGRAM_ABI_VERSION` goes from `1u` to `2u`, per the pre-1.0 rule of breaking changes only with no
compatibility machinery. **The cost of this breaking change is currently zero**: there is no header
to edit, no Program repository consuming it, and `tglGetProgramVTable` is unimplemented on the Grid
side too.

One scope caution, because it is easy to violate by accident. The ABI delivers **energy per band per
time bin per ear, and stops.** Anything that names a source, separates streams, reports "a wall is
three metres to your left", or performs auditory scene analysis of any kind belongs in a Program
repository.

---

## The Humming Neon

**Decision: the neon geometry is the sound source, and its fundamental is 3 kHz.** Light and sound
radiate from the same triangles, and "one Grid, two senses" becomes literal rather than aspirational.

This section states the research's objection in full, agrees with the part of it that is correct, and
then states plainly which part of the decision is a departure from physics.

### The objection, stated properly

The objection is correct as far as it goes, and it has two halves.

**Audibility.** The audible hum of a gas-discharge fixture is not produced by the glowing gas column.
On the standard engineering account it comes from the magnetic ballast, whose laminated core is
squeezed and released by magnetostriction at twice the mains frequency — 100 Hz on 50 Hz mains, 120 Hz
on 60 Hz. Set the Grid's neon humming at 100 Hz and consult the roster. *C. elegans* hears it, since
its range starts at exactly 100 Hz. A macropod might, at the very bottom edge of a proxy range starting
near 500 Hz. **A mouse-class creature is flatly deaf to it**, with a 60 dB lower limit of 2.3 kHz, more
than four octaves above. The insect presets would not detect it beyond a few millimetres regardless,
because it is the wrong field. So the most architecturally elegant option would leave the Grid
silent to most of the roster, and the one creature it serves best is the one that cannot localise
anything anyway.

**Validity.** At 100 Hz the wavelength is 3.43 m. The pillars are 0.26 wavelengths across, the terrace
steps 0.24, the glowing column 0.41, the widest glass slab 1.75, and the tube emitting the sound is
0.015 wavelengths wide. **Every object on the Grid except the terrace levels is at or below one
wavelength.** This is the exact regime in which ray tracing produces its worst answers: it would report
confident, detailed, entirely fictional occlusion patterns behind objects that real 100 Hz sound flows
around almost undisturbed, and the errors would be *systematic*, so a creature would learn to rely on
shadows the physics does not produce.

Both halves are correct and neither is disputed here.

A note on the citation, because it matters more than it looks. The twice-mains magnetostriction
mechanism could only be verified in popular and trade sources, which is below this repository's
standard. It is stated above as the standard engineering account and is **not cited**, and no reader
should treat it as a sourced physical claim.

### Why 3 kHz

The fundamental is set at 3 kHz because that is the only place it can go and reach the whole roster.

*C. elegans* hears 100 Hz to 5 kHz. The house mouse hears 2.3 to 85.5 kHz. **The intersection is
2.3–5 kHz, and it is the only window in which the simplest and the most acoustically capable creatures
on the roster can hear the same sound.** Three kilohertz sits in the middle of it.

Harmonics extend the reach upward and cost nothing, because a source spectrum is applied once at
emission. The second harmonic at 6 kHz and the third at 9 kHz are both well inside the mouse's range,
and the third lands almost exactly on the macropod proxy's best frequency of 8–10 kHz; harmonics from
the seventh upward fall in the 20–50 kHz band a noctuid tympanal ear is tuned to, should such a preset
ever exist. One fundamental serves the bottom of the ladder and its harmonic series serves the top.

The in-world justification is that the Grid does not run on 1970s magnetic ballasts. **High-frequency
electronic ballasts switch at 20–60 kHz and are sold as hum-free** precisely because they move the
excitation out of the audible band, so a tube's acoustic frequency is not pinned to mains and becomes a
design parameter. It is also worth noting that 3 kHz sits inside the middle of Steam Audio's three
production bands (800 Hz to 8 kHz, centred at 2.5 kHz), so the choice lands in the band a shipping
product treats as its principal one.

### The departure, stated plainly

**Choosing 3 kHz specifically is a deliberate departure from real physics, made to serve the roster. The
physics did not force it; the creature list did.**

Nothing in acoustics says a gas-discharge tube sings at 3 kHz. What the physics says is that the
frequency is set by the drive electronics, and what this document does is choose drive electronics that
produce a frequency the creatures can hear. That is a design decision dressed in a plausible in-world
justification, and it should be read as such. Anyone who wants a physically faithful ballast hum should
set the fundamental to 100 Hz, accept that most of the roster will hear nothing, and accept that the
ray tracer's answers below 500 Hz are fiction.

### What the hum is actually like at 3 kHz

Three consequences follow from the choice, and one of them corrects a conflation in the objection.

**A sub-wavelength object is a poor scatterer and a perfectly good source.** At 3 kHz the tube is 0.44
wavelengths wide, so it will never scatter sound and the wavelength ledger is right about that. But we
are not asking the tube to scatter; we are asking it to *emit*, and a sub-wavelength radiator is simply
an omnidirectional line source. The objection's "the tube is a sixty-ninth of a wavelength across"
argument applies to the tube as an obstacle, not to the tube as an emitter, and the distinction is the
reconciliation the decision needs.

**The tube and its floor image merge.** The tube sits 1 cm above the floor, so tube and image are 2 cm
apart — 0.175 wavelengths at 3 kHz. The pair is effectively coincident, and the hum therefore radiates
from the floor surface into a half space rather than from a suspended strip into a full one. That is a
real gain of up to 6 dB in pressure at the source and it means the acoustic emitter is, correctly, the
ground.

**Eight thousand three hundred and twenty continuous emitters is the wrong object for a source-driven
tracer and free for a listener-driven one.** A spatially dense, statistically uniform, continuously-on
source lattice produces a nearly static field, and a time-domain histogram tracer is a tool for
transients. Shooting from 16,640 triangles is not merely expensive, it is the wrong algorithm.
**Gathering finds them by hitting them**, which is exactly what `trace.slang` already does with
`material.emission`, and the cost is completely independent of how many tubes there are.

Air absorption barely touches the fundamental: interpolating ISO 9613-2's 20 °C / 70 % table between
2 kHz (9.0 dB/km) and 4 kHz (22.9 dB/km) gives roughly 14 dB/km at 3 kHz, so about 1.8 dB across the
entire 128 m of the Grid. The harmonics are where the spectral tilt appears — 8 kHz loses 9.8 dB over
the same span — and that tilt is a genuine, learnable distance cue on the Grid, where vision is
deliberately unreliable.

The hum also buys something the research noticed and is worth keeping: a persistent ambient bed with
genuine position dependence, from which a creature could infer coarse altitude and terrace level where
the Grid's mirror floor makes vision untrustworthy. That is the same argument PERCEPTION.md already
makes for vestibular sensing, arriving through a different sense.

**Creature vocalisation is not excluded and should follow.** It makes the Grid's acoustics dynamic, it
delivers echolocation for free once hearing exists, and it is the case that exercises the point-source
enumeration path. It is Phase 5b, not a competitor.

---

## What Phase 5 Rejects

Each of these is rejected with a reason stronger than cost.

**All wave methods — FDTD, adaptive rectangular decomposition, precomputed wave field coding, Project
Acoustics.** This is a physics-regime mismatch, not a compute budget. FDTD's load grows with the fourth
power of frequency, and Aalto's research pages name heavy memory consumption at high frequencies as the
fundamental problem of all wave-based techniques. ARD attacks the constant factor and remains a
fourth-power problem. What that buys in production is documented precisely by Raghuvanshi and Snyder:
they fix the maximum simulated frequency at **500 Hz**, precompute on a 140-machine cluster, and report
bake times of 4 to 20 hours per scene with raw field data of 9 to 66 TB compressed to tens of megabytes.
Behaviour above 500 Hz is not simulated at all — it is extrapolated parametrically. **The most
sophisticated production wave-acoustics system in the industry stops two to seven octaves below where
the Grid's listeners begin.** Scaling 500 Hz to 85.5 kHz is a frequency ratio of 171 and, at
fourth-power scaling, a factor of roughly 850 million in work. Beyond that, the method assumes static
geometry, needs a bake step and an asset format, and ships as binary plugins — three of this
repository's constraints, each independently fatal. Mehra and colleagues' equivalent-source formulation
is the wave community's answer for large open scenes and is likewise precomputed. Reject permanently,
not merely defer.

**UTD edge diffraction and its four prerequisite subsystems.** Keller's geometrical theory of
diffraction made diffraction expressible in a ray framework at all; Kouyoumjian and Pathak's uniform
theory fixed the divergence at the shadow and reflection boundaries; Tsingos, Funkhouser, Ngan and
Carlbom brought it into interactive virtual environments with the "diffraction only in shadow regions"
simplification that has been standard ever since. UTD is cheap once you know which edge you are on. The
cost is everything before that, and Schissler, Mehra and Manocha give the best accounting:
dihedral-angle edge classification, hierarchical voxelisation at the shortest simulated wavelength,
marching cubes, coincident-vertex welding to recover adjacency, quadric-error simplification, collinear
edge merging, and a precomputed edge-to-edge visibility graph. Their measured costs run to 178.5 s of
simplification and a 23.8 MB graph taking 299.5 s to build for a large city. **This repository has none
of the preconditions and actively destroys the main one**: `geometry.hpp` states that all generators
emit per-face, flat-shaded vertices with no shared-vertex indexing, so `indices` is always the trivial
sequence, and the BVH build then reorders triangles so each leaf owns a contiguous range. There is no
edge table, no half-edge structure, no dihedral information, and no welding pass anywhere. And the
result would be absurd: the 8,320 flat, zero-thickness neon quads alone yield 33,280 candidate free
edges, against roughly forty box edges and a few hundred riser edges that actually matter — every one
of the 33,280 being exactly the short edge Schissler warns produces inaccurate UTD results, on geometry
that does not exist acoustically below 6.9 kHz anyway. Reject. The mitigating fact is recorded under
[defer](#what-phase-5-defers).

**BTM as a runtime feature.** Biot and Tolstoy derived the exact time-domain solution for an infinite
rigid wedge; Medwin extended it to finite barriers with experimental validation; Svensson, Fred and
Vanderkooy recast it into the form everyone implements — diffraction as a line integral of analytically
derived directional secondary sources. It is exact where UTD is asymptotic, it degrades gracefully at
shadow boundaries, and it yields an impulse response directly. It is also a spatial quadrature per
source-edge-receiver triple, and its output is a **phase-accurate pressure** impulse response. The
deliverable here is an octave-banded energy histogram, so BTM would do the expensive part of the
calculation precisely in order to throw the expensive part away. Reject as a runtime feature — but
remember it as an **offline validation reference**, computing the true field behind one pillar to check
whatever Phase 5 actually does. `libs/bvh` already has the precedent: `intersectBruteForce` exists
solely as the reference the hierarchy is checked against.

**Beam tracing and frustum tracing.** Both replace the point ray with a volume, and both exist to make
the specular early response receiver-independent and exact. Funkhouser and colleagues' beam trees give
exact clipping and cheap receiver movement at the price of a precomputed spatial subdivision over
static geometry — which a per-frame BVH rebuild cannot supply. Lauterbach, Chandak and Manocha's frustum
tracing and its adaptive successor trade exactness for tolerance of moving geometry. Both would mean a
second, structurally different traversal algorithm living alongside the single-ray BVH walk, to solve a
problem this scene does not have. Directly contradicted by the repository's governing rule.

**Acoustic radiance transfer and every precomputed transfer method.** Siltanen, Lokki, Kiminki and
Savioja's room acoustic rendering equation is the paper that made the graphics-acoustics analogy
formal, which is exactly the analogy this repository is built on, and it deserves the citation. But the
data structure is a patch-to-patch transfer matrix over static geometry, sharing nothing with a BVH, and
its known weakness — blurred specular paths — is precisely where this scene's short, sparse response
lives. The Grid moves.

**RT60, Sabine, Eyring, the Schroeder frequency and any statistical reverberator.** Argued above.
ISO 3382-1's definition contains the word *enclosure* and this scene is not one.

**Auralisation of every kind**, and **every perceptual model**. Argued under [Scope](#scope).

## What Phase 5 Defers

Each of these has a written trigger, so that "later" means something checkable.

- **Point sources, and everything built to serve them.** Direct-occlusion rays against a point source
  and image-source enumeration for a point source are both specified under
  [The Build Proposal](#the-build-proposal) and neither is written in code in the first cut, because
  the only point source in the design is a creature vocalisation and that is Phase 5b. Building the
  second mechanism before its use case exists is the rule-4 violation this document rejects beam
  tracing for. **Trigger: the vocalisation action lands in the ABI.**
- **Diffuse rain / secondary sources.** The technique — turning every surface hit into a source that
  radiates explicitly towards the receiver, which is next-event estimation by another name — is
  correct, lets the receiver be a point, and traces to Heinz's 1993 grafting of statistical scattering
  onto an image-source solver, with Rindel's ODEON description and Schröder's RAVEN work as the modern
  references. Its cost is one visibility ray per surface hit per listener. **Trigger: an enclosed space
  exists on the Grid** — a tunnel, a hollow structure, anything with a ceiling. Until then, rays escape
  after one or two bounces and there is almost nothing for it to smooth.
- **A hand-authored diffracting-edge list.** The correct edge list for the Grid is not derivable from
  the triangle soup, but it *is* knowable a priori, because the Grid is generated procedurally: the ten
  boxes' forty vertical edges could be emitted by `generateBox` in about twenty lines, and if the risers
  ever become vertical they are known to the generator too. Feed those to a single-edge Maekawa
  attenuation applied only in shadow regions. **Trigger: a creature demonstrably failing a task because
  a terrace or pillar shadow is a hard zero** — not a wish for realism.
- **Coherent summation of the direct-plus-floor pair.** Summing energy rather than pressure costs at
  most +3 dB where +6 dB is correct, and unbounded error on the destructive side. Working the
  ground-effect comb filter for creature-scale geometry puts the first destructive notch at 17 kHz to
  214 kHz for sources and listeners at 2–10 cm over ranges of 0.5–2 m — far above where it would matter,
  and octave banding washes out everything past the first notch or two anyway. Ship the pure energy
  histogram and **document the systematic +3 dB bias on the strongest path in the scene**, because that
  is the kind of thing that is very hard to notice later and very easy to record now.
- **Per-band absorption.** Trigger: a material that is not a hard planar surface. The three-band form is
  free in memory should it ever be wanted — a `float3` plus a `float` fills a 16-byte std430 row exactly
  — and expensive only in the trace loop.
- **Acoustic transmission.** Trigger: a creature demonstrably blocked on the direct path *and* every
  reflected path simultaneously. On an open floor with strong terrace bounces this is close to
  impossible.
- **Temporal ray reuse.** Schissler, Mehra and Manocha's diffuse cache is the single highest-value
  optimisation in the literature: 1,000 rays per frame with the cache matches 10,000 without it, 2.27 dB
  average error against 6.69 dB. It is also, strictly, temporal accumulation — the thing PERCEPTION.md
  rule 6 forbids on the visual path. It is defensible for hearing because real ears do integrate energy
  over milliseconds, but the literature's two-second cache constant is far longer than any auditory
  integration window and would make a creature's ears sluggish in a way no animal's are. **Trigger: a
  measured solve that is too slow.** If adopted, choose the time constant from auditory integration, not
  from the paper's default.
- **Vertical risers.** Strictly a geometry change rather than an acoustics one, but it is the change
  with the largest acoustic payoff available, and it is the precondition for monostatic echolocation.
  Recommended, sized above at 1,386 triangles.

---

## The Build Proposal

### Data

- **BVH.** Bind the same `nodes` and `triangles` buffers, unchanged. The three buffer handles are
  currently private members of `Tracer` with no accessor; the acoustic pass is the concrete second use
  case the repository's own rule demands before a small extraction, so either add three accessors or
  hoist buffer ownership into a small `World` holder.
- **Acoustic materials.** A new `StructuredBuffer<float2>` of six `(absorption, source strength)`
  pairs, indexed by the same material index, bound only by the acoustic pipeline. `Material`,
  `trace.slang` and all six `static_assert`s are untouched.
- **The hum spectrum.** One `float4` per solve, the Grid's 3 kHz fundamental and its harmonics resolved
  into the listening ear's four bands. It is a push constant, not a material field, because every
  sounding surface on the Grid shares it.
- **Air absorption.** An eight-entry constant table from ISO 9613-2's 20 °C / 70 % row (0.1, 0.3, 1.1,
  2.8, 5.0, 9.0, 22.9, 76.6 dB/km at 63 Hz to 8 kHz), extended above 8 kHz by evaluating ISO 9613-1's
  formulae **once, offline**, and pasting the resulting constants in beside the tabulated ones — the
  standard notes explicitly that its formulae extend to ultrasonic frequencies. Nothing evaluates a
  relaxation-frequency model at run time while the Grid is fixed at 20 °C and 70 %; if temperature
  ever becomes a scene parameter, that is when the formulae move into the code. The numbers must
  appear in the documentation rather than living silently in a constant.
- **Per-listener ring buffer.** The Grid owns the delay line. Sixty-four bins of 1 ms per band per ear
  covers the proposed 20 m total-path cap with room to spare: 2 KiB per two-eared creature, 40 KiB for
  twenty of them.

### The pass

Four steps per ear per solve, all against the existing hierarchy — but only the last two are built in
the first cut, because the first two exist solely to serve point sources and there are no point sources
until Phase 5b.

1. **Direct occlusion — specified now, built with Phase 5b.** For each point source, one any-hit ray
   from source to listener. Neither `BvhLib::intersect` nor `trace()` has an early-out variant — both
   compute the *nearest* hit and tighten the limit as they go. Adding one is about twenty lines on each
   side and no BVH change, and the two must move together because `libs/bvh/tests/bvh_tests.cpp` exists
   precisely to hold them to each other. **Make the result a fraction, never a bit.** The single largest
   error available in this whole subsystem is "ray blocked implies silence", stated most bluntly by
   Microsoft's Project Triton page: in a ray model "a thin lamppost blocking the ray from source to
   listener can occlude as much as a concrete wall". Steam Audio's fix is to model the source as a
   sphere and sample several points within it, capped at 16 samples. Half a dozen extra rays turn a
   binary shadow into a graded one. **It is not diffraction and must never be described as
   diffraction** — it removes the discontinuity, and the discontinuity is the artefact that would
   actually break a creature's behaviour.
2. **Enumerated image sources — specified now, built with Phase 5b.** For point sources only: six
   terrace levels plus about fifty outward-facing box faces, each reflected and validated with one BVH
   ray that confirms both that the reflection point lies on a face at that level and that the path is
   clear. Roughly 57 candidates per source-listener pair. Note that no pre-filter decides which of the
   six levels are plausible for a given listener: six is small enough that the validation ray *is* the
   plausibility test, which is why the listener's own terrace level never has to be determined and no
   height-lookup machinery is needed. It is written down here so the histogram layout is not chosen in
   ignorance of it, and it is not written in code until there is a vocalisation to enumerate.
3. **Deterministic gather**, for the hum bed and for everything the enumeration cannot reach — the
   risers, oblique paths, second order. Cast N directions from the ear, accumulate path length, and on
   every hit whose material has a non-zero `acoustic_source_strength`, deposit that strength times the
   hum spectrum into the bin the accumulated delay selects, exactly as `trace.slang` deposits radiance.
   **The directions come from a spherical Fibonacci set indexed by the ray id, never from an RNG.**
   There is no random number generator anywhere in this repository, PROGRAM_INTERFACE.md publishes
   bit-identical replay as a guarantee, and PERCEPTION.md rule 6 forbids adding smoothing for a Program's
   benefit. A deterministic quasi-uniform set gives the same count and the same coverage,
   bit-identically, every run.
4. **Reduce and deliver.** One workgroup owns one ear. The histogram lives in shared memory as `uint`s —
   64 bins by 4 bands is 1 KiB against Vulkan's guaranteed 16 KiB — so there is **no cross-workgroup
   atomic contention at all**. Float atomics are not available (`VK_EXT_shader_atomic_float` is not
   requested and requesting it would break the device policy of Vulkan 1.3 core plus swapchain only) and
   this design does not need them.

The fixed-point scale is a real question and it is answered by the range cap rather than dodged by it.
With total path capped at 20 m, spreading costs at most 26 dB relative to a one-metre reference, air
absorption at 8 kHz adds 1.5 dB over that distance, and ten bounces at `alpha = 0.02` add 0.9 dB. **The
whole delivered response therefore spans under about 30 dB — a factor of a thousand, not many orders of
magnitude.** So one scale suffices: put a unit relative arrival at `2^18`, which leaves roughly eight
bits of resolution on the quietest arrival the cap permits and, against a worst case of 2,048 directions
by four orders, 8,192 full-strength deposits landing in one bin — `2^18 × 2^13 = 2^31`, inside a `uint`
with a bit to spare. Those two numbers are coupled, so **assert the product at dispatch time** rather
than discovering the wrap in a histogram. The quantisation floor and the saturation bound both belong in
the shader's header comment, next to the scale.

Conventions that must be chosen once and written down:

- **Explicit `1/r²`, never a detection sphere.** The two are mutually exclusive and mixing them
  double-counts spreading. Detection spheres exist to make a stochastic ray count converge in a closed
  room; the Grid is an open plane with a point receiver it can afford. The failure mode of getting
  this wrong is a silent 6 dB per doubling that looks like a material problem.
- **Range cap of 20 m of total accumulated path**, replacing the hardcoded `10000.0` in `trace.slang`
  with a parameter. Justified three ways: air absorption at 30 kHz costs 14 dB over 20 m and 90 dB over
  128 m, so the Grid has a physical acoustic horizon; Schnitzler and Kalko bound useful echolocation at
  10.5 m; and a distance cap bounds traversal cost predictably. Lawrence and Simmons' figures make the
  point sharply — beyond about 7 m a 100 kHz call loses more to the air than to the inverse square law,
  and at 8 kHz the same crossover is out past 400 m. **The Grid is smaller acoustically than
  visually, and that is physics rather than a budget.**
- **Reflection order cap of 4.** Rays escape after one or two bounces here; four is generous.

### Acceptance, and how this is checked

The gather has to be checkable or it is not engineering, and the repository already owns the pattern:
`intersectBruteForce` exists solely as the reference `libs/bvh/tests/bvh_tests.cpp` holds the hierarchy
to. The acoustic equivalent is a host-side gather that walks **every** triangle for every direction,
with no hierarchy at all, and compares the resulting histogram bin for bin against the GPU result. Four
acceptance criteria, in increasing order of what they prove:

1. **Determinism.** Two runs of the same scene, same listener, same direction count produce
   byte-identical histograms. This is the one the Fibonacci direction set exists to make possible, and
   it is a cheap continuous check rather than a one-off.
2. **Brute force agreement.** GPU gather against host brute force, bin for bin, to within the
   fixed-point quantum. Any disagreement is a traversal bug, not an acoustics question.
3. **One analytic case.** A single listener over a single flat terrace level with one sounding triangle
   directly above has a direct arrival and exactly one image arrival, both at delays computable by
   hand. If the two bins are not where arithmetic says they are, the delay accumulation is wrong.
4. **Energy sanity.** With `alpha` set to zero and the range cap set beyond the Grid, no path may gain
   energy, and the sum over all bins must fall monotonically as absorption rises. This catches the
   double-counted spreading the convention note above warns about.

None of these needs a reference implementation from the literature, which is the point: the model is
small enough that its own arithmetic is the specification.

### Budget

Every figure below is derived from this repository's own profiler output. Phase 2 is the clean
measurement because it is exactly one traversal per pixel: **921,600 primary ray-segments in 3.97 ms, or
about 230 million segments per second** against ~24,950 triangles on a GPU with zero ray-tracing
extensions. Acoustic rays scatter more incoherently than a coherent primary camera ray, so the honest
bracket is **150–230 M segments/s**, and both columns are given.

| Configuration | Segments | At 230 M/s | At 150 M/s |
|---------------|---------:|-----------:|-----------:|
| One ear, 2,048 directions × 4 orders | 8,192 | 36 µs | 55 µs |
| One two-eared creature, one solve | 16,384 | 71 µs | 109 µs |
| Steam Audio real-time default (4,096 rays × 4 bounces) | 16,384 | 71 µs | 109 µs |
| Twenty two-eared creatures, one solve round | 327,680 | 1.42 ms | 2.18 ms |
| **The same at 10 Hz, spread over 60 fps** | **54,613 per frame** | **0.24 ms/frame** | **0.36 ms/frame** |

For scale, the entire bloom pyramid plus tone mapping plus sRGB encode costs **0.31 ms**. A
twenty-creature acoustic solve, amortised, costs about what post-processing costs.

For a second scale, the published GPU acoustic systems are not a ceiling to aspire to. iSound reported
18–20 million ray intersections per second on a GTX 280; the follow-up work needed a cost-guidance
algorithm to shrink both the visibility ray count and the receiver radius because the naive
configuration was too slow even on a GTX 480. **This repository's hand-written compute traversal already
delivers roughly an order of magnitude more throughput on a laptop GPU with no ray-tracing hardware.**
The one genuinely modern GPU path, NVIDIA's VRWorks Audio 2.0 on OptiX 6 with RT cores, is proprietary
and RTX-dependent and is therefore precisely the thing this project is not doing.

Three caveats keep the budget honest:

1. **The histogram write is not in the 230 M/s figure.** Depositing band energy into time bins is a
   scattered write with no analogue in the visual tracer. The shared-memory design above should make it
   cheap, but it has not been measured and deserves a microbenchmark before the layout is fixed.
2. **Register pressure is the measured cost driver here, and an acoustic thread is fatter.** Every
   thread already allocates `uint stack[30]` for traversal plus `RayTask stack[3]`. The sweep table in
   `trace.slang` shows the task stack alone costing 15.24 ms against 19.00 ms at depth six, with the two
   axes multiplying rather than adding. A thread carrying a delay accumulator per outstanding task pays
   this again. Budget for it rather than discovering it.
3. **Everything contends for one GPU.** Valve's own documentation warns about exactly this for Radeon
   Rays: it "requires care to ensure that it doesn't take up too much GPU processing time from graphical
   rendering or other GPU workloads". The mitigation is already implied by the 10 Hz rate — stagger each
   creature's solve phase so roughly one creature in six solves on any given frame, and never dispatch
   the whole round at once.

**Realistic bottom line: 0.24 to 0.36 ms per frame for twenty two-eared listeners**, which is under 3 %
of the current frame and about the cost of the post-processing chain, with an order of magnitude of
headroom before the number becomes interesting. The binding constraint is **listener count, not ray
count** — the opposite of the intuition one brings from room acoustics. Note also that the User's
window is the worst case that will ever exist: a creature sensor is a fourteenth to a two-hundredth of
that frame, so a headless creature-only run frees essentially the whole budget.

### Hazards recorded before they bite

- **Profiler.** Adding an `Acoustic` enumerator to `GpuPass` **requires a matching string in
  `PASS_NAMES`**. That array is sized from `GpuPass::Count` and value-initialises the new slot to
  `nullptr`; `logSummary()` then constructs a `std::string` from a null pointer once per second. Nothing
  guards the pairing, so add a `static_assert`. Separately, an every-Nth-frame pass is **under-reported
  by its own duty cycle**: on frames where it records nothing the availability check contributes a zero
  and the EMA blends it in, so a 12 ms solve every eighth frame reports as roughly 1.5 ms. Either gate
  the EMA on availability or report cost per solve, and say which.
- **Shared Slang modules will not trigger rebuilds.** `add_slang_shader` sets `DEPENDS` on the single
  source file only. Factoring the traversal into a shared `.slang` module imported by both shaders means
  edits to it rebuild neither `.spv` and the build runs stale SPIR-V. Extend the CMake function with an
  explicit extra-dependency argument; do **not** copy 150 lines of traversal, which is exactly the drift
  the repository's asserts exist to prevent.
- **Push constants.** The acoustic pass wants a different block entirely — listener and ear frames,
  direction count, band count, bin count and width, the hum spectrum, speed of sound, range cap, order
  cap, node count — so it is a new push struct and a new pipeline, not a variant. The tracer uses 80 of
  the 128 bytes Vulkan guarantees; stay inside 128 or move to a UBO.
- **Do not put listeners in the BVH.** It is rebuilt from a `std::vector<Triangle>` by value, and a
  moving listener would mean a rebuild every frame. Gather from the ear, or loop over the handful of
  listeners outside the hierarchy.
- **`fresnelSchlick` must not be reused for sound.** Specular reflection is the same law for both senses
  and `reflect()` transfers exactly. An air-solid impedance mismatch reflects nearly everything, so
  acoustic transmission is a coefficient rather than a Snell bend, and there is no acoustic Fresnel to
  weight it with.

### Documentation that must be corrected in the same commit

Several documents still describe a state the code left behind, and a design written against them would
inherit a false premise.

`docs/ARCHITECTURE.md` § Material Model has already been corrected once, but two of its sentences are
made false by this document rather than by the code: it says "Phase 5 will therefore **have to touch
this layout**, and that is the intended cost", which the parallel-buffer recommendation above makes
untrue, and its Phase 5 section applies "acoustic absorption and scattering coefficients", when
scattering is resolved here to zero and given no field at all. Its buffer-binding block also still lists
`binding 0 bvh_nodes / 1 triangles / 2 materials` with `absorption, scattering, flags` — the real set is
`0` output image, `1` nodes, `2` triangles, `3` materials — and describes triangles as
`[v0, v1, v2, normal, material_index]` when they are `v0, material, edge1, pad, edge2, pad`. Its Phase 5
section also still claims "the material record was designed from the start to carry both sets of
properties".

`docs/VISION.md` says every surface carries "both optical and acoustic properties" "stored side by
side", and `README.md` design principle 5 says surfaces "carry optical and acoustic properties
together"; both need the wording adjusted to coupling by material index rather than by struct row.
`src/tracer.hpp` still comments the material table as "64 bytes each", the last code-side fossil of the
four-row struct. `TODO.md` still names `hearing_samples`, which has never existed and which this
document recommends replacing with the per-ear view above.

---

## Things This Document Declines to Quantify

Following [PERCEPTION.md](PERCEPTION.md)'s convention: where the physics or the biology does not
translate cleanly, this document says so rather than inventing a number.

- **Absolute sound pressure levels.** There is no reference level on the Grid. The histogram carries
  energy relative to the emitting source, `acoustic_source_strength` is relative to a primary neon tube,
  and any dB SPL figure would be fabricated.
- **A scattering coefficient for any surface.** It is zero by construction, and a non-zero value would
  be an unmeasurable fudge standing in for geometry that either exists in the mesh or does not exist at
  all.
- **Quokka hearing, in any respect.** No audiogram exists. The `macropod` band is a dasyurid range plus
  tammar wallaby ear biophysics, and nothing whatever from *Setonix brachyurus*.
- **The rat's 60 dB hearing range.** The widely circulated "530 Hz to 68 kHz" could not be confirmed from
  any retrieved source. What is quotable is the tested range of 250 Hz to 70 kHz and the 42 kHz upper
  limit of good hearing.
- **The mechanism of a real ballast hum, as physics.** The twice-mains magnetostriction account appears
  only in popular and trade sources, below this repository's citation standard, and is therefore stated
  as an assumption and cited nowhere.
- **Elevation localisation for anything but the mouse.** Every other minimum-audible-angle figure quoted
  here is azimuthal, and median-plane acuity is several times worse.
- **The observable an insect ear measures, in ray-traced terms.** Johnston's organ senses particle
  velocity in the near field; an energy histogram computes far-field pressure energy. Rather than invent
  a conversion, the insect presets get no ears at all.
- **What any of this sounds like.** There is no auralisation stage and no waveform. The deliverable is a
  buffer of numbers.
- **The ray-count saving attributable to millisecond binning.** PERCEPTION.md quotes "at least 40×" from
  Vorländer; that specific figure could not be verified against the cited edition. The qualitative claim
  — that millisecond-scale histogram binning substantially reduces the required ray count — stands.

---

## References

Every source is listed, with what it supports, in
[research/acoustics.md](research/acoustics.md#references).
