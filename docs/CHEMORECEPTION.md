# Chemoreception

Food on the Grid, the body's sense of taking it in, and smell: what each is, which block owns it, and why it is
shaped as it is. A design whose wire half is built (link v12), and whose two senses reach a Program in `TglSenses`
(Program ABI 11); the world's half is Master Control's own build. The
owner asked for it on 2026-10-29 ("glowing yellow orbs" for food, then white, and smell "as realistic as we did with
the auditory and visual senses"), and iso-lab asks for both
([master-control#63](https://github.com/ai-quokka-wannabe/master-control/issues/63),
[tron-grid-lite#153](https://github.com/ai-quokka-wannabe/tron-grid-lite/issues/153)).

It reverses one recorded decision. [PROGRAM_INTERFACE.md § Chemoreception](PROGRAM_INTERFACE.md#chemoreception-built-at-abi-11)
left smell out because "adding it would mean a diffusing scalar field, the first subsystem the BVH cannot help with
at all". Both halves of that stay true, and neither is a cost: the field below is not simulated on a grid but
written down in closed form, and it needs no BVH because odour, unlike light and sound, is not traced.

## Scope

- **Food**: a world entity Master Control owns - placed, eaten and gone by the world's rules, replayed by Clu like
  every other fact of the world.
- **Intake**: the body's sense of what it swallowed this tick. Interoception, a fact about the creature's own body,
  like the joint angles and loads.
- **Smell**: the concentration of odour at the creature's two amphids, read through receptors that saturate and
  count molecules, as a nose does.

Out of scope, each with its trigger in [Deferred](#deferred-with-triggers): taste on contact, wind, odour blocked by
the terraces, odours that are not food, and a creature that chooses what it eats.

## The rule this document keeps

[PERCEPTION.md § Design rules](PERCEPTION.md#design-rules) and the owner's ruling that preventing cheating is
paramount: **a sense hands a Program only what a real body's receptors would measure.** No food position, no
direction to food, no label, no "food remaining", no valuation. A Program that wants to know where food is must
infer it from how the reading changes as its body moves - which is exactly what the worm does
([What the worm does with it](#what-the-worm-does-with-it)).

## Who owns what

| Responsibility | Block | Why |
|---|---|---|
| Where food is, how much is left, when it is gone | Master Control | Ground truth; the RNG of record places it; replay needs it in the world |
| Whether a mouth is in food, and what it swallowed | Master Control | A contact rule on the world's own geometry, like the floor's |
| The odour field and the receptors' reading | Master Control | Physics of the world, deterministic, built on the world's own transcendentals; reported in the letter like the joint loads |
| Drawing the orbs; the eyes seeing their glow | the Grid (spectator, creature host) | Presentation and sense rendering, as for everything that glows |
| Turning the letter into `TglSenses` | creature host | Copies, derives nothing |

Smell is computed where the world is, not where the senses are, for a reason that differs from the eyes and ears:
it needs no GPU and nothing the host has that the world lacks, and computing it in Master Control keeps it inside the
replay - Clu re-simulates a life and gets the same readings bit for bit. The eyes and ears stay host-side because the
hardware they need is there.

## Food: glowing white orbs

- **What an orb is**: a sphere resting on the floor, radius 0.25 m, emissive **white** (equal linear radiance in
  every channel, times a strength the world sets), holding a quantity of food that eating depletes. A shrinking
  quantity shrinks the orb's glow, not its size, so a half-eaten orb is a dimmer one. White, not the first draft's
  yellow, is the owner's ruling (2026-10-29): a bright ball every animal can see. Yellow would have been invisible to
  the worm - the `elegans` preset keeps only the blue channel and clamps green and above to zero
  ([PERCEPTION.md](PERCEPTION.md#preset-elegans-there-is-no-image)) - and white carries blue, so the worm's two
  photoreceptors see an orb as every other eye does.
- **Where orbs are**: placed at world start by the RNG of record, on flat cells at least an orb's diameter from any
  riser. Deterministic, so two lives of one world meet the same food.
- **How much food - set when Master Control starts** (the owner's ask, 2026-10-29). Today Master Control takes only
  command-line flags (`--disk`, `--disk-roll`, `--log`, `--verbose`) and has no config file, and the world's shape is
  compiled into both ends of the wire. Food needs neither end to agree in advance, because only the world places it
  and the orbs travel in `TICK_STATE`, so its density is a launch setting rather than a world-definition constant:
  `--food-density <orbs per 100 m^2 of open floor>`, and the same key in an optional `--config <file>` of
  `key = value` lines (Master Control's first config file: every flag may be written there, and a flag on the
  command line wins). The density and the placement seed are written into the input log's header, so Clu replays
  the same food; a Disk needs nothing extra, since the orbs are in its frames.
- **Seen**: the orbs are geometry in the scene the senses already trace. A creature with eyes sees their glow and
  their reflection in the mirror floor; a creature's ears hear them as occluders. Nothing about this is special-cased:
  they are emissive objects, as the neon tubes are. A creature whose preset has no eyes cannot see them at all - the
  worm's case, and the reason smell matters.
- **Gone, with a pew**: an orb eaten to nothing is removed, with an `EVENT` (kind: food gone) - and it goes with a
  sound, the owner's "pew" (2026-10-29): a short falling chirp emitted at the orb, as a call is. It is a real source in
  the world, not a speaker effect: the spectator's speakers play it through the same mixer as every call, and every
  creature's ears receive it by the same acoustics, so a creature nearby hears that food was eaten - which is exactly
  what an animal near a feeding rival could learn, and nothing more. Its odour does not vanish with it - see
  [the trace](#the-field).

**What the glow means is the brain's business, and the real worm makes it interesting.** Food every animal can see is
food a creature may find by light as well as by smell, and the glow is a world parameter that can be dimmed to none
without touching anything else. For the worm there is a twist worth keeping rather than designing away: the real
animal treats short-wavelength light - blue-violet and ultraviolet - as a potent avoidance cue, through the LITE-1
protein ([Edwards et al. 2008](https://doi.org/10.1371/journal.pbio.0060198)), and white light carries the short
wavelengths. A brain faithful to the worm may therefore be drawn to an orb by its odour and pushed from it by its
light. The sense reports intensity only; whether light is aversive is the Program's, and the conflict is a
measurement iso-lab can make.

## Intake: the mouth and the pump

A worm eats by pumping its pharynx: in the presence of bacterial food, after a two-hour fast, N2 worms pump at 3.46 ± 0.19 Hz
([Hiebert, Chicas-Cruz and McCormick 2017](https://doi.org/10.17912/W2CC7Z), microfluidic recordings of two minutes).
How the rate moves with hunger and with scarce food is reported in the literature that paper builds on and is not
read here yet; the world uses the measured rate as it stands.

- **The mouth** is the head segment's nose tip, the front of segment zero on its axis.
- **In food** when the mouth lies inside an orb: its distance from the orb's centre is less than the radius.
- **The pump**: while the mouth is in food the world pumps at 3.5 Hz on the tick grid (a pump on every ninth tick at
  32 Hz, 3.56 Hz - the nearest the tick allows), and each pump swallows a fixed bite, removed from the orb.
  Pulsatile on purpose: a worm's intake arrives in pumps, and a Program that sees a steady trickle is being told
  something no gut says.
- **The sense**: `TglSenses` gains `intake`, what the body swallowed this tick, in the world's food unit: zero on
  most ticks, one bite on a pump. A body class without a mouth reads zero always.

Hunger, satiety and whether intake is good are the Program's business, as the interface already says of pain.

## Smell

### The field

Odour leaves a source and spreads by diffusion through still air over the floor, which reflects it, while some of it
is lost - absorbed by the floor, broken down - at a first-order rate. For a point source switched on at time zero,
emitting at rate `q`, the concentration at distance `r` after time `t` has a closed form, the classical continuous
point source with first-order loss (Carslaw and Jaeger's treatment of a continuous point source):

```text
F(r, t) = q / (8 pi D r) * [ exp(-r/L) erfc( r / (2 sqrt(D t)) - sqrt(k t) )
                           + exp(+r/L) erfc( r / (2 sqrt(D t)) + sqrt(k t) ) ]        L = sqrt(D / k)
c(x, t) = F(|x - s|, t) + F(|x - s'|, t)
```

**Derived, not quoted.** A unit release at time zero in still air spreads as the Gaussian kernel
`G(r, tau) = exp(-k tau) (4 pi D tau)^(-3/2) exp(-r^2 / 4 D tau)`, the loss contributing the `exp(-k tau)`; a
source emitting `q` continuously from time zero is that kernel integrated, `c = q * integral_0^t G(r, tau) dtau`,
and the integral has the closed form `F` above (the classical result for a continuous point source, Kelvin's
construction, treated at length by Carslaw and Jaeger). Rather than rest on a page number, Master Control's tests
integrate the kernel numerically and hold `F` to it, before and after steady state, and hold the `k = 0` limit to its
`erfc` form - the formula proves itself in CI, where a citation could only be trusted.

Here `s` is the orb's centre and `s'` its mirror image below the floor: the floor reflects odour, and the method
of images turns a reflecting plane into a second source. With no loss (`k` = 0) `F` is the familiar
`q erfc(r / 2 sqrt(D t)) / (4 pi D r)`. Three properties are why this is the right field:

- **It is exact** for the physics it states, not a fit. It grows from nothing after the source appears, settles to
  the steady `q exp(-r/L) / (4 pi D r)` per source, and falls off with distance the way an odour plume in still air does.
- **It is cheap**: a sum over the orbs, each a couple of `exp` and `erfc` evaluations per receptor per tick. No grid,
  no GPU, nothing a modest machine notices.
- **It remembers.** An orb eaten to nothing is not erased from the field: by superposition, a source switched off at
  time `t1` is the same source plus an equal negative one switched on at `t1`, so its odour lingers and fades as real
  odour does. These are the "blurring old traces" iso-lab's hunch B2 asks of a sense of smell.

**Scale.** The Grid's worm is 4.48 m long (eight segments at 0.56 m); the real worm is about 1 mm. Time runs at real
speed. So that a body length of the Grid's worm sees the field a real worm's body length sees, the diffusion
coefficient is scaled by the square of the length ratio: `D_grid = D_air * (4.48 m / 1 mm)^2`. For a small volatile
molecule in air `D_air` is of order 1e-5 m^2/s. The anchors read: at 25 °C carbon dioxide diffuses in air at
0.16 cm^2/s (1.6e-5 m^2/s) and water vapour at 0.26 cm^2/s (Incropera, *Fundamentals of Heat and Mass Transfer*, as
tabulated by Cussler's *Diffusion*), and a gas diffuses more slowly the heavier its molecule, so the worm's odorants
(diacetyl and isoamyl alcohol are 86 and 88 g/mol, against carbon dioxide's 44) sit below carbon dioxide - 1e-5 m^2/s
is that order. *The odorants' own measured coefficients are not read yet; the constant stays a named world parameter
until they are.* That gives `D_grid` of order 2e2 m^2/s: in one
second an odour spreads about three body lengths, on the Grid as on the plate. The loss rate `k` and each orb's
emission `q` are world parameters, chosen so a feeding patch is smellable from a few body lengths to a few tens.

**Determinism.** The world owns its transcendentals (`master-control/src/trig.rs`); `exp` and `erfc` join `sin`,
`cos` and `atan2` there, built from IEEE basic arithmetic only (fdlibm's), so a life replays bit for bit on any
machine.

### Odours are fingerprints, not labels

Each orb carries an odour that is a mixture: a fixed, non-negative pattern over **eight** receptor types, drawn per
orb from the RNG of record. A receptor of type `j` sees the field times the pattern's `j`-th weight; mixtures add.
The worm senses many volatile odorants through only a few neuron types
([Bargmann, Hartwieg and Horvitz 1993](https://doi.org/10.1016/0092-8674(93)80053-h): "six distinguishable volatile
odorants are sensed by only two types of sensory neurons"), so an odour is a pattern of activity across types, not a
name. Eight is a design choice, small enough for a worm-sized brain, large enough for iso-lab's B2 to measure
generalisation and blends in.

### The receptors

The worm's amphids are paired sense organs at the nose, left and right. On the Grid:

- **Two amphids**, left and right of the mouth on the head segment, a body's width apart. Their difference is real
  but small, as it is in the worm - which is why the worm learns direction by swinging its head and moving, not by
  comparing its two sides.
- **Each amphid reports eight readings**, one per receptor type: the fraction of its receptors bound, `c / (c + K)`,
  which saturates as real binding does; a reading is never a concentration.
- **Noise, from physics**: a receptor estimates concentration by counting molecules that arrive, and the counting has
  a floor no organism beats - its relative error falls with diffusion, receptor size, concentration and averaging
  time ([Berg and Purcell 1977](https://doi.org/10.1016/s0006-3495(77)85544-6)). Each reading carries noise of that
  size, drawn from the RNG of record, so it is deterministic and replays, and faint odour is uncertain odour.
- **No adaptation in the sense.** Real olfactory neurons adapt, and their output tracks change more than level; that
  is computation, and the Program does it. The sense reports bound fractions; the brain differentiates.

### What the worm does with it

What a Program can build on this, and what makes it a fair test: the worm finds odour sources by two strategies that
work together - pirouettes, turns whose onset tracks the rate of change of concentration rather than its level
(Pierce-Shimomura, Morse and Lockery 1999), and weathervaning,
a gradual curve up the gradient ([Iino and Yoshida 2009](https://doi.org/10.1523/JNEUROSCI.3633-08.2009)). Both need
only a reading at the nose and the body's own motion, which is what this sense gives and all it gives.

## On the wire, and in the header

| Where | What |
|---|---|
| World definition | unchanged: food is placed only by the world, so neither end must agree on it in advance; the density and the placement seed live in Master Control's launch settings and the input log's header |
| `TICK_STATE` | food rows (`LnkFoodState`, 24 bytes): id, position, radius, quantity left - so the spectator draws them and the creature host's senses see them; at most 64 orbs, 1,536 bytes. No odour pattern: smell is the world's to compute, and no client needs the patterns |
| `EVENT` | food gone (`LNK_EVENT_FOOD_GONE`): the tick, the orb's centre as the position, the creature whose bite emptied it, and the pew's strength - no orb id; the sound a spectator and the ears render |
| `PROPRIOCEPTION`, the letter | `intake` this tick; `odour[16]`, the two amphids' bound fractions (left 0-7, right 8-15); the letter grows from 96 to 160 bytes |
| `TglSenses` | `intake` and `odour[2][8]` (`TGL_AMPHIDS` by `TGL_ODOUR_RECEPTORS`), copied from the letter; 216 bytes. **Built** (Program ABI 11) |

A protocol bump (v12, with client ABI 10) and a Program ABI bump (11), taken together, as movements 3 to 5 took theirs.

## The build, in order

1. **Master Control**: `exp` and `erfc` in `trig.rs`, held to the platform's within a tolerance; `food.rs` (placement,
   the mouth rule, the pump, the quantity); `odour.rs` (the field, the receptors, the noise); unit tests for each, red
   first - the field against its own steady state and its superposition, the pump against the tick grid, placement
   against the risers.
2. **Link** v12: the food rows, the event, the letter's new fields; the protocol fingerprint moves, the world's does not. **Built** (link#49, merged 2026-10-29).
3. **Master Control** on the wire: rows, event, letter; Clu replays food; the goldens re-recorded.
4. **The Grid**: the orbs drawn (emissive, in the scene the senses trace), `TglSenses` grows, ABI 11; checked on every
   adapter the desks have and on lavapipe. `TglSenses` and ABI 11 are **built**: the host copies `intake` and the
   sixteen readings from the letter and derives nothing.
5. **rc-worm**: the panel shows the amphids and the pumps, so a User at the panel smells what the worm smells.

## Deferred, with triggers

- **Taste on contact** (the worm's ASE neurons taste salt in what it touches): when a world has dissolved chemistry.
- **Wind** (advection skews the plume): when a world has moving air.
- **Odour around the terraces**: the field above treats the Grid as an open half-space over one flat floor, so a
  riser neither shadows odour nor reflects it. The physics says this is a small error rather than a missing feature:
  odour fills the air above the floor, not a layer along it, and in one second it spreads about 14 m - seventeen
  times a riser's 0.83 m - so a step perturbs the field only within a few step heights of itself, as a stone in a
  wide stream does. The trigger is a measured behaviour that differs because of it near an edge; the fix then is a
  solved field, the cost this document avoids until it is owed.
- **Odours that are not food** (repellents, other creatures): when a world has a second kind of source.
- **Choosing what to eat**: when food has kinds a body can tell apart and should.

## References

- Berg HC, Purcell EM (1977). Physics of chemoreception. *Biophysical Journal* 20(2):193-219.
  doi:10.1016/s0006-3495(77)85544-6
- Bargmann CI, Hartwieg E, Horvitz HR (1993). Odorant-selective genes and neurons mediate olfaction in *C. elegans*.
  *Cell* 74(3):515-527. doi:10.1016/0092-8674(93)80053-h
- Pierce-Shimomura JT, Morse TM, Lockery SR (1999). The fundamental role of pirouettes in *Caenorhabditis elegans*
  chemotaxis. *Journal of Neuroscience* 19(21):9557-9569.
- Iino Y, Yoshida K (2009). Parallel use of two behavioral mechanisms for chemotaxis in *Caenorhabditis elegans*.
  *Journal of Neuroscience* 29(17):5370-5380. doi:10.1523/JNEUROSCI.3633-08.2009
- Hiebert T, Chicas-Cruz A, McCormick K (2017). Reduced pharyngeal pumping rates observed in tph-1 mutants using
  microfluidic electropharyngeogram (EPG) recordings. *microPublication Biology*. doi:10.17912/W2CC7Z
- Carslaw HS, Jaeger JC. *Conduction of Heat in Solids*: the classical treatment of point sources. Not cited for a
  page: the field is derived above and checked numerically in Master Control's tests.
- Edwards SL et al. (2008). A novel molecular solution for ultraviolet light detection in *Caenorhabditis elegans*.
  *PLoS Biology* 6(8):e198. doi:10.1371/journal.pbio.0060198
- Incropera FP et al. *Fundamentals of Heat and Mass Transfer*, 6th ed. - diffusion coefficients of gases in air at
  25 °C, as tabulated in Cussler EL (1997), *Diffusion: Mass Transfer in Fluid Systems*, 2nd ed.
