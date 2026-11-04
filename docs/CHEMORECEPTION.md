# Chemoreception

Food on the Grid, the body's sense of taking it in, and smell: what each is, which block owns it, and why it is
shaped as it is. A design whose wire half is built (link v12), whose two senses reach a Program in `TglSenses`
(Program ABI 11), and whose orbs and pew the Grid draws and sounds; the world's half is built in Master Control. The
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
- **Taste** (ruled 2026-11-04, [below](#taste-ruled-2026-11-04)): chemistry by contact - what dissolves out of food
  into the floor, and what a mouth or skin touches.
- **Oxygen and carbon dioxide** (ruled 2026-11-04, [below](#oxygen-and-carbon-dioxide-ruled-2026-11-04)): the air's
  gases, which food and bodies breathe in and out, and the senses that read them.

**Every sense here is for any animal.** The world computes fields - air, floor, heat - that know nothing of who
breathes or tastes them; a body declares its organs with the body it rezzes, and the world samples its fields at
those organs. The worm is the first body and the worked example throughout, never the template: what is said of its
amphids, its ASE or its URX is one declaration among the many other bodies will make.

Out of scope, each with its trigger in [Deferred](#deferred-with-triggers): wind, odours that are not food, and a
creature that chooses what it eats. Odour around the terraces is ruled in, below.

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
- **How much food - set when Master Control starts** (the owner's ask, 2026-10-29). Master Control's other launch
  settings are command-line flags (`--disk`, `--disk-roll`, `--log`, `--verbose`), and the world's shape is
  compiled into both ends of the wire. Food needs neither end to agree in advance, because only the world places it
  and the orbs travel in `TICK_STATE`, so its density is a launch setting rather than a world-definition constant:
  `--food-density <orbs per 100 m^2 of open floor>`, and the same key in an optional `--config <file>` of
  `key = value` lines (Master Control's first config file: every flag may be written there, and a flag on the
  command line wins). The density and the placement seed are written into the input log's header, so Clu replays
  the same food; a Disk needs nothing extra, since the orbs are in its frames.
- **Seen**: the orbs are geometry in the scene the senses already trace. A creature with eyes sees their glow and
  their reflection in the mirror floor; a creature's ears hear them as occluders. Nothing about this is special-cased:
  they are emissive objects, as the neon tubes are. A creature whose preset has no eyes at all cannot see them - and the worm's
  two photoreceptors see an orb only as a little more light, which is why smell matters.
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

### On the Grid, as built

- **The orbs** (`src/orbs.hpp`): one unit icosphere, subdivided twice (320 faces), scaled to the row's radius and
  carried to its centre. The glow follows the quantity in eight steps, from a crumb at an eighth of full to a whole
  orb at a linear radiance of 4.0 in every channel - its brightest channel level with the neon's, below the pillars'. Stepped
  rather than continuous because that is the material model's shape: a triangle's slot is the only key the tracers
  have and the table is uploaded once, so each step is its own small geometry wearing its own emissive slot, and a
  bite that crosses a step changes which shape an orb stands in - a placement, never a re-upload, and no new shader
  feature. An orb that is still told never goes dark.
- **In both scenes**: the spectator's stage (`src/world_stage.hpp`) places one record per orb on the tracer's dynamic
  instance path; the creature host's stage (`src/stage.hpp`) stands one instance per orb after every body and guest,
  in the scene the eyes trace and the ears walk, so an eye sees an orb's glow and its reflection in the floor and an
  ear meets it as an occluder. A telling with no food renders exactly as before - the shapes wait as geometry no
  instance names, and the reference render, which never joins a world, is untouched.
- **The pew** (`src/audio.hpp`): a sine falling exponentially by two octaves, 1,760 Hz to 440 Hz over a quarter of a
  second, on a 90 ms decay, at the event's strength; placed by the orb's centre with the same pan and distance as a
  call, and still - its source is the orb, not the eater, so it carries no Doppler. The creature host relays each
  food-gone event of the tick as a pew the senses sound as a call: energy in the bins its distance dictates and a
  discrete arrival, since a chirp has the sharp onset a scratch lacks.

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
- **Each amphid reports eight readings**, one per receptor type: the fraction of its receptors bound, which saturates
  as real binding does; a reading is never a concentration. A receptor type is a **population of affinities**, 21 of
  them log-uniform over five decades centred on `K` (master-control#87), and the reading is the population's mean of
  `c / (c + K_i)`: roughly the logarithm of the concentration across the middle, exactly a half at `K`. Five decades
  because the worm follows a point source of diacetyl across a 100,000-fold range
  ([Bargmann, Hartwieg and Horvitz 1993](https://doi.org/10.1016/0092-8674(93)80053-h), as cited by
  [Larsch et al. 2015](https://flavell.mit.edu/wp-content/uploads/2016/02/A-Circuit-for-Gradient-Climbing-in-C.-elegans-Chemotaxis.pdf)),
  partly with receptors of different affinity - ODR-10 in AWA for low concentrations
  ([Zhang et al. 1997](https://pmc.ncbi.nlm.nih.gov/articles/PMC23737)), another receptor in ASH for high ones - and
  partly by adapting, which stays the Program's (below). One affinity alone spans about two decades, and with every
  orb's odour summed it read near full almost everywhere on the floor (master-control#76).
- **Noise, from physics**: a receptor estimates concentration by counting molecules that arrive, and the counting has
  a floor no organism beats - its relative error falls with diffusion, receptor size, concentration and averaging
  time ([Berg and Purcell 1977](https://doi.org/10.1016/s0006-3495(77)85544-6)). Each reading carries noise of that
  size, drawn from the RNG of record, so it is deterministic and replays, and faint odour is uncertain odour.
- **No adaptation in the sense.** Real olfactory neurons adapt, and their output tracks change more than level; that
  is computation, and the Program does it. The sense reports bound fractions; the brain differentiates.

### The air, the noses and the smoke (ruled 2026-11-04)

The owner's ruling, for every animal the Grid will hold and not only the worm: **Master Control owns the air and the
diffusion; each Program owns its nose; humans see the odour as smoke, animals only smell it.**

- **The air is the world's.** Today's field is the exact closed form for a point source, but in an empty half-space:
  a riser neither shadows nor channels odour, and the air is still. It becomes a **numerical diffusion field** solved
  by Master Control on a grid over the floor, in which risers are walls odour does not pass, so it fills a hollow,
  rounds a corner and is shadowed behind a terrace. The solver is implicit (alternating-direction), so it is stable at
  the 31.25 ms tick, deterministic in f64 like the rest of the world, and cheap on one CPU core; an eaten orb's odour
  still fades as real odour does. Its constants - the diffusivity, the loss rate - are properties of **the Grid's
  air**, the same for every body that breathes it, not of one creature (the closed form's diffusivity was scaled to
  the worm's length). Wind, advection along a world airflow, is the next step behind a written trigger: the first
  animal whose sensing a plume matters to.
- **The nose is the Program's.** The sense organs become part of the body a Program declares at `REZ`, as its servos
  are: where each organ sits on the body, how many receptor types it has (within a cap the wire carries), and each
  type's affinity population. The world samples its air at whatever organs a body declares. The worm keeps its two
  amphids of eight types; a mammal might declare two nostrils with many more, and later sniffing - a sampled volume
  per breath. No adaptation in the sense, for any animal: that is the brain's.
- **The smoke is the human's.** A human in the Grid has no nose, so the spectator window draws the odour field as
  **green smoke** whose density follows the concentration, ray-marched through the same grid, behind a key. It exists
  for the people watching and only for them: **it is never in a creature's senses pass**, so no animal ever sees odour -
  they smell it, which is the only honest way to have it (the rule this document keeps).

The order: this ruling; the air on the grid in Master Control, with the figures regenerated to show walls at work; the
organ declaration on the wire (a Link bump) and in the Program ABI, the worm declaring its amphids; then the smoke in
the spectator window.

### Taste (ruled 2026-11-04)

Smell is chemistry through the air; taste is chemistry by contact, or dissolved in water. Animals taste with very
different organs - a mammal's tongue, a fly's taste sensilla on its feet and mouthparts, a fish's taste buds over its
skin - but the physics is one: a receptor touching a solution reads what is dissolved in it. The worm, the first body,
does both senses: AWA and AWC smell volatiles, while its ASE pair tastes what is dissolved in the agar - salt above
all, whose gradients it climbs (Bargmann and Horvitz 1991; ASEL fires as the salt rises and ASER as it falls,
[as reviewed here](https://pmc.ncbi.nlm.nih.gov/articles/PMC6744849), and a well-fed worm returns to the salt it was
raised on, [Frontiers 2018](https://www.frontiersin.org/articles/10.3389/fnmol.2018.00484)). On the Grid, in the
split of the rest of this document:

- **Food has a taste.** Each orb carries a taste profile - a non-negative pattern over a few water-soluble compound
  types (sugars, salts, acids, bitter ones) - drawn per orb from the RNG of record, separately from its odour, so a
  food can smell one way and taste another and a Program learns which to trust.
- **The floor's solutes are the world's.** Master Control owns a second, slower field: what an orb leaches into the
  floor beneath it, diffusing **in the floor, not the air**. A solute in water diffuses about ten thousand times
  slower than an odour in air - sodium chloride in water is 1.611e-9 m^2/s at 25 °C, by the Nernst-Hartley relation from
  its ions' limiting conductances (master-control#92 shows the sources) - so scaled as the air is, a taste spreads
  about 2.8 m in a minute (2 sqrt(D t)), less than a body length: rings and trails
  around food, not plumes. A riser is a wall in the floor too; an eaten orb's taste lingers and fades like its smell.
- **Taste organs are each Program's**, declared with the body like its nose: where each organ sits (a mouth, a
  tongue, feet, skin that lies on the floor) and its receptor types, each a population of affinities as smell's are.
  **An organ tastes only what it touches**: the floor's solutes directly under it, or an orb it is inside. Nothing at
  a distance, for any animal. The worm declares its ASE pair at the nose; a mammal might declare a tongue reading
  sweet, salty, sour, bitter and savoury, an insect its feet. Learning and asymmetry - ASEL and ASER, the memory of a
  salt - are each Program's.
- **Humans** need no picture of taste - they see the food - though the window may show a faint sparkle where an
  animal takes a bite, so a spectator knows when it is tasting.
- **Later, behind a trigger**: taste tied to what food is worth - a bitter orb that nourishes less or harms - the
  first step to choosing what to eat.

### Oxygen and carbon dioxide (ruled 2026-11-04)

Every animal that breathes senses its gases, though with different organs and for different ends: a mammal's carotid
bodies read oxygen and its brainstem reads carbon dioxide to drive its breathing, a mosquito finds its host by the
carbon dioxide on the breeze, and the worm, the first body, lives by its oxygen - it strongly prefers 5 to 12 percent
and avoids both more and less ([PMC4500246](https://pmc.ncbi.nlm.nih.gov/articles/PMC4500246);
[Gray et al. 2004](https://deepblue.lib.umich.edu/bitstream/2027.42/62676/1/nature02714.pdf)). Its URX neurons fire as
oxygen rises (the soluble guanylate cyclases GCY-35 and GCY-36), its BAG neurons as oxygen falls and as carbon
dioxide rises (GCY-9), and its avoidance of carbon dioxide is muted near the air's 21 percent oxygen by URX's tonic
signal ([PMC3721734](https://pmc.ncbi.nlm.nih.gov/articles/PMC3721734)). Food matters here too: food is alive, a
bacterial lawn breathes, so a food patch is low in oxygen and high in carbon dioxide, and an animal can read both.

- **The gases are the world's**: two more scalars in Master Control's air grid, beside the odour and the
  temperature, by the same solver with risers as walls, relaxing to the open air - 21 percent oxygen, about 0.04
  percent carbon dioxide - at the grid's top. Their diffusivities are the gases' own in air, scaled as the air is
  (carbon dioxide's 1.6e-5 m^2/s is cited above; oxygen's, about 2e-5, to be read from the same table).
- **Everything that lives breathes.** Each orb consumes oxygen and releases carbon dioxide. Each body breathes **from
  the same metabolism that makes its heat** ([THERMORECEPTION.md](THERMORECEPTION.md#body-heat)): one declared
  metabolic rate warms the body, draws down the oxygen around it and raises the carbon dioxide, so a crowd of animals
  leaves stale, warm air behind it, as a crowd does - whether it is worms or anything larger.
- **Gas senses are each Program's**, declared with the body: where each sits and which gas it reads, outside the body
  or inside it. A reading is the gas's fraction at that place, with the receptor's noise. The worm declares its URX
  and BAG pairs; a mammal might declare the inside readings that set its breathing; an insect, carbon dioxide
  receptors on its palps. Preferences - the worm's 5 to 12 percent, a mosquito's pull to breath - and how one gas
  tempers another are each animal's behaviour, the Program's to learn, never built into a sense.
- **Humans** see nothing of the gases by default: the smoke and the aura already show where food and warmth are, and
  more tints would cloud the window. A later toggle can show them if a spectator needs it.

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
   against the risers. **Built**.
2. **Link** v12: the food rows, the event, the letter's new fields; the protocol fingerprint moves, the world's does not. **Built** (link#49, merged 2026-10-29).
3. **Master Control** on the wire: rows, event, letter; Clu replays food. **Built** (master-control#72 and #73); the
   bodies' hash does not cover food yet, so the golden held.
4. **The Grid**: the orbs drawn (emissive, in the scene the senses trace), `TglSenses` grows, ABI 11; checked on every
   adapter the desks have and on lavapipe. `TglSenses` and ABI 11 are **built**: the host copies `intake` and the
   sixteen readings from the letter and derives nothing. The orbs and the pew are **built** too - in the spectator's
   picture and speakers and in the creature host's eyes and ears ([as built](#on-the-grid-as-built)).
5. **rc-worm**: the panel shows the amphids and the pumps, so a User at the panel smells what the worm smells.

## Deferred, with triggers

- **Wind** (advection skews the plume): when a world has moving air.
- **Odour around the terraces** - now ruled in ([The air, the noses and the smoke](#the-air-the-noses-and-the-smoke-ruled-2026-11-04)),
  so this entry records why it waited: the field above treats the Grid as an open half-space over one flat floor, so a
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
