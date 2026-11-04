# Thermoreception

Heat on the Grid and the sense that feels it: what each is, which block owns it, and why it is shaped as it is. A
design, ruled by the owner on 2026-11-04 and not yet built: "we should address heat ... humans see it as a kinda
redish aura ... more red the hotter, the less red the cooler ... more subtle as otherwise it will bloat the spectator
view. and animals again can't see the heat aura but instead just simply feel it". It follows the split
[CHEMORECEPTION.md § The air, the noses and the smoke](CHEMORECEPTION.md#the-air-the-noses-and-the-smoke-ruled-2026-11-04)
rules for smell: **Master Control owns the heat, each Program owns its thermoreceptors, and humans see an aura that no
animal ever sees.**

## Scope

- **Ambient temperature**: the temperature of the Grid's air and floor, a field Master Control computes, the same for
  every body in it.
- **Body heat**: every body's own temperature, from its metabolism and its exchange with the air around it - and every
  body, in turn, a small source of heat in the field.
- **Thermoreception**: a body's sense of the temperature where its thermoreceptors sit - what the worm's AFD
  neurons feel, and what its famous thermotaxis is built on - and its sense of its own body heat.
- **The aura**: the spectator window's picture of the field, for the humans watching.

Out of scope here: **radiant warmth**. A body already reads one coarse radiance figure, its `irradiance`, traced by the
Grid ([PERCEPTION.md § The senses that are not eyes](PERCEPTION.md#the-senses-that-are-not-eyes)), as a pit viper's
pit organ reads infrared. That sense stays as it is; this document adds the other one, the temperature of the
surroundings, which a worm feels through its body and its sensory endings rather than through light.

**For any animal.** The temperature field knows nothing of who feels it, and every body declares its own thermal
make-up and thermoreceptors with the body it rezzes. Animals feel warmth with very different organs - a mammal's
warm and cold receptors spread through its skin, a mosquito's heat sensors on its antennae that find a warm host,
a snake's pit organs for radiant heat (the Grid's `irradiance`), the worm's AFD pair - and each is one declaration of
the same sense. The worm is the first body and the worked example below, never the template.

## The rule this document keeps

[PERCEPTION.md § Design rules](PERCEPTION.md#design-rules), and the owner's ruling that preventing cheating is
paramount: a body may feel only what a real body at its place would feel. A thermoreceptor reports the temperature
at its own position, with the noise a real one has, and nothing else - no gradient vector, no map, no location of the
warmest spot. The aura is the humans' substitute for the sense they lack in the Grid; **it is never in a creature's
senses pass**, so no animal ever sees heat. It feels it.

## Who owns what

| Block | Owns |
| --- | --- |
| Master Control | The temperature field: the floor's thermal map, the energy pillars' and the bodies' heat, the air's diffusion of it, all in the air grid the odour shares; every body's own temperature; the launch settings that shape them |
| A Program | Its body's thermal make-up and its thermoreceptors, declared with its body at `REZ` like its servos and its nose: its metabolic heat and heat capacity; where each thermoreceptor sits and how finely it resolves |
| The Grid (creature host) | Nothing new to compute: the world's letter carries each declared thermoreceptor's reading, as it carries the amphids' |
| The Grid (spectator) | The aura, drawn from the field, for humans only |

## Heat on the Grid

### The field

Heat in still air spreads by diffusion, as odour does, so it lives in the same numerical grid Master Control solves
for the air ([CHEMORECEPTION.md](CHEMORECEPTION.md#the-air-the-noses-and-the-smoke-ruled-2026-11-04)), as a second
scalar beside the odour: one implicit, deterministic solver, two fields, risers as walls for both. The floor is the
reservoir: in the worm's world the agar holds the temperature and the thin air above it follows, so each floor cell
has a temperature and the air exchanges heat with it.

- **Diffusivity**: air's thermal diffusivity is 22.5e-6 m^2/s at 300 K (Incropera, *Fundamentals of Heat and Mass
  Transfer*, Table A.4). It is scaled by the Grid's air scale exactly as the odour's diffusivity is, so the two fields
  stay one air: 451.6 m^2/s on the Grid (master-control#90), which crosses the 128 m floor in well under a minute.
- **The floor's thermal map**, a world setting: an ambient temperature and, optionally, a linear gradient across the
  floor in a chosen direction. The default gradient is the classical thermotaxis assay's: about **0.5 °C per
  centimetre**, chosen to resemble moist soil (Hedgecock and Russell 1975, as described by
  [Raiders et al. 2022](https://bio-protocol.org/pdf/Bio-protocol4370.pdf)). A centimetre of the real worm's world is
  44.8 m of the Grid's (4.48 m / 1 mm), so that is about 1.4 °C across the floor.
- **The energy pillars are heat sources** (the owner's ruling): the Grid's glowing pillars warm the air around them,
  each a standing source in the field, so the air is warmer near a pillar and a body feels it from a few body lengths
  off. Their heat output is a world setting, chosen and cited in the PR that builds it.
- **Every body is a heat source too**, at its own temperature (below). Other warm patches or cold ones can be added as
  world settings; each is a source in the same field, so it spreads and fades by the same physics.

### Body heat

Each body has a temperature of its own, which Master Control steps every tick by heat balance: the metabolic heat
the body makes, which its Program declares at `REZ` with its heat capacity, against what it exchanges with the air
around it. The body is then a source in the field at that temperature, so a warm body leaves warm air behind it, and
another animal can feel it.

The worm makes the physics plain: it is a cold-blooded animal of a millimetre, its own heat is tiny against the air's,
and its body temperature follows its surroundings - as the real worm's does. A warm-blooded animal declared later
would hold its own temperature above the air's and leave a trace of warmth wherever it went. Nothing in the world
knows which kind an animal is: it is what the declared numbers make of the same equation.

### Why a worm can climb it

The worm responds to temperature changes **as small as 0.01 °C** across a range of more than 10 °C, mostly through its
single pair of AFD neurons ([Goodman and Sengupta 2018](https://pubmed.ncbi.nlm.nih.gov/29218454/); the channel's
sensitivity, [Ramot, MacInnis and Goodman 2008](https://pmc.ncbi.nlm.nih.gov/articles/PMC2504807)). Within about 3 °C
of the temperature it was raised at, it tracks isotherms to within about 0.05 °C
([PMC6758190](https://pmc.ncbi.nlm.nih.gov/articles/PMC6758190); [Luo et al. 2006](https://samuel.physics.harvard.edu/sites/scholar.harvard.edu/files/aravisamuel/files/j_exp_biol_2006_luo.pdf)).
On the Grid's default gradient 0.01 °C is about 0.9 m of floor, a fifth of a body length: a gradient fine enough to
be honest and coarse enough to climb.

## The thermoreceptors: the Program's

- **Declared with the body**, as its nose is: each thermoreceptor's position on the body and its resolution. The worm
  declares one pair at the nose, its AFDs, with a noise floor of about 0.01 °C; a larger animal might declare many
  over its skin.
- **A reading is a temperature at that place**, in kelvin, with the receptor's noise drawn from the RNG of record, so
  it is deterministic and replays. A real thermoreceptor transduces exactly that - cooling closes AFD's channels and
  warming opens them (Ramot 2008) - and an animal does feel absolute warmth and cold.
- **The body's own heat is felt too**: every body reads its own temperature as interoception, beside its joint angles
  and its gut - the warmth of the body from inside, as an animal feels being cold or overheated. Outside heat comes
  through the thermoreceptors at their places; inside heat through the body itself. The two agree while the body
  matches its air and part when it does not, and that difference is information a Program can use.
- **No adaptation and no memory in the sense.** The worm's preference for the temperature it was raised at, and AFD's
  shifting threshold, are learning and computation, which is the Program's. The sense reports the temperature; the
  brain remembers and compares.

## The aura: the human's

The spectator window draws the field as a **red aura**, redder where the air is hotter and fading out where it is
cooler, ray-marched through the same grid as the smoke and toggled by a key. **Subtle on purpose**: at most a faint
tint over the scene, scaled to the field's range so a small gradient is still visible and a large one never washes
the picture out. It is for the people watching and only for them, and it never reaches a creature's senses.

## The build, in order

1. This ruling.
2. The temperature field in Master Control, in the air grid (with the odour's): the floor's thermal map as launch
   settings, the energy pillars as sources; a figure of the field over the floor, as smell has.
3. Body heat: each body's temperature stepped by its declared metabolism and heat capacity, and each body a source in
   the field.
4. The thermoreceptors and the body's thermal make-up declared with the body, and the readings - outside temperature
   at each receptor, the body's own temperature - in the letter: on the wire and in the Program ABI, in the same bump
   as the noses' declaration; the worm declaring its AFD pair.
5. The aura in the spectator window.

## References

- Goodman MB, Sengupta P (2018). The extraordinary AFD thermosensor of *C. elegans*. *Pflügers Archiv* 470:839-849.
  [PubMed 29218454](https://pubmed.ncbi.nlm.nih.gov/29218454/)
- Ramot D, MacInnis BL, Goodman MB (2008). Bidirectional temperature-sensing by a single thermosensory neuron in
  *C. elegans*. *Nature Neuroscience* 11:908-915. [PMC2504807](https://pmc.ncbi.nlm.nih.gov/articles/PMC2504807)
- Hedgecock EM, Russell RL (1975). Normal and mutant thermotaxis in the nematode *Caenorhabditis elegans*. *PNAS*
  72:4061-4065 - the linear and radial gradients, as described in
  [Raiders et al. 2022, *Bio-protocol*](https://bio-protocol.org/pdf/Bio-protocol4370.pdf).
- Luo L, Clark DA, Biron D, Mahadevan L, Samuel ADT (2006). Sensorimotor control during isothermal tracking in
  *C. elegans*. *Journal of Experimental Biology* 209:4652-4662.
  [PDF](https://samuel.physics.harvard.edu/sites/scholar.harvard.edu/files/aravisamuel/files/j_exp_biol_2006_luo.pdf)
- Incropera FP et al. *Fundamentals of Heat and Mass Transfer*: air's thermal diffusivity at 300 K.
