# Related Work

What other people have built in this area, what this project borrows from them, and — stated
plainly — which parts of the idea are not new at all.

## Why this document exists

It is easy for a project like this one to sound more novel than it is. Giving a simulated creature
biologically honest senses and watching what it does is not a speculative idea: it is an active,
well-funded subfield with peer-reviewed tools, and several of them do parts of this better than
this project will.

So this page serves three purposes. It credits the work whose measurements and ideas the rest of
the documentation leans on. It gives anyone writing a creature brain a reading list far better than
anything here. And it states honestly what is unusual about this project and what is simply
ordinary — because a contributor deserves to know which is which before spending an evening on it.

## Rendering senses the way animals have them

**[CompoundRay](https://elifesciences.org/articles/73893)** (Millward et al., *eLife*, 2022) is the
closest published relative of this renderer's vision system, and it prefigures nearly every
decision made in [PERCEPTION.md](PERCEPTION.md). It ray-traces insect compound eyes with ommatidia
placed on arbitrary curved surfaces, renders a bee's roughly 6,000-ommatidium view at **over 3,000
frames per second**, and represents an eye as a per-ommatidium sample vector rather than a raster —
the same conclusion `TglEyeDesc` reaches by a different route. Its authors note that ray casting
was chosen partly so that **mirrors and lenses render correctly**, which is a pleasing coincidence
for a world made entirely of mirrors.

**[I2Bot](https://royalsocietypublishing.org/rsif/article/22/222/20240586/90881/)** (*Journal of the
Royal Society Interface*, 2025) builds on that with a multi-modal embodied simulator aimed at
insect navigation, integrating compound-eye rendering into a full agent loop.

Anyone seriously interested in insect vision should start with these rather than with this
repository.

## Simulating bodies and the brains that drive them

**[NeuroMechFly v2](https://www.nature.com/articles/s41592-024-02497-y)** (*Nature Methods*, 2024,
EPFL) is a morphologically accurate adult *Drosophila* derived from X-ray microtomography, with 65
body segments and 122 degrees of freedom, sensory input, motor feedback and complex terrain. Where
this project treats a body as a position with a few sensors bolted on, NeuroMechFly models the
actual animal.

**[A virtual rodent predicts the structure of neural activity across
behaviours](https://www.nature.com/articles/s41586-024-07633-4)** (Aldarondo et al., *Nature*, 2024
— Harvard and Google DeepMind) trains an artificial network to actuate a biomechanically realistic
rat by imitating freely moving animals. The striking result is the validation: activity in the
virtual controller predicted real neural activity in sensorimotor striatum and motor cortex
**better than the real rat's own movements did**.

**[Whole-body physics simulation of fruit fly
locomotion](https://www.nature.com/articles/s41586-025-09029-4)** (*Nature*, 2025) continues the
same line, and work embodying connectome-derived brain emulations in such bodies has since been
[reported](https://eon.systems/updates/embodied-brain-emulation) — though that last one is a
company announcement rather than a peer-reviewed result, and is listed here with that caveat.

This is the frontier, it is genuinely hard, and this project is not attempting it.

## Testing animal-like cognition

**[The Animal-AI Environment](https://link.springer.com/article/10.3758/s13428-025-02616-3)**
(Voudouris et al., *Behavior Research Methods*, 2025; first released 2019, and the basis of the
2020 Animal-AI Olympics) is a virtual laboratory for comparative cognition: 900 task
configurations across 10 levels, drawn directly from the comparative-psychology literature —
double T-mazes, object permanence, the cylinder task, Thorndike's puzzles.

It is worth knowing about for the opposite reason to the others. Animal-AI asks *whether an agent
can solve a task an animal can solve*; this project deliberately provides no tasks, no rewards and
no scoring at all. Anyone who wants to measure a creature rather than merely watch it should look
there.

## Embodied-AI environments generally

Habitat, DeepMind Lab, VizDoom, Procgen, MineRL and the vision-language-action robot policies are
covered in [PERCEPTION.md](PERCEPTION.md#what-embodied-ai-practice-actually-feeds-to-networks),
where their observation resolutions supply the empirical argument for tiny sensors. The short
version: a decade of that research converged on observations of 64×64 to roughly 112×112 pixels,
and published null results show that raising the resolution frequently changes nothing.

## What is actually unusual here

Not much, and it is worth being precise about which parts:

- **The world is deliberately hostile to vision.** The simulators above render naturalistic
  environments — real ant habitats, realistic terrain, ordinary rooms. This one is perfect mirrors
  and emissive neon on infinite black, where reflected space is geometrically indistinguishable
  from real space. That is an awkward environment for a spatial learner, on purpose.
- **One world, many kinds of animal.** The research tools are necessarily species-specific, because
  a laboratory studies one animal. A single world offering sensor presets from a two-scalar worm to
  a macropod, all tracing the same acceleration structure, is uncommon mainly because nobody with a
  research question needs it.
- **The brain is behind a plain C ABI.** Research simulators are tightly coupled to Python and to a
  particular neural-modelling framework, which is right for a laboratory and wrong for a playground.
  Here the world knows nothing whatever about how a brain works, which is closer to a game engine's
  mod interface than to a scientific instrument.
- **One acceleration structure for sight and sound.** Multi-modal simulators exist; building a
  single BVH so that a creature's ears are traced against exactly the geometry its eyes see is an
  engineering convenience rather than a research contribution, but it does shape the whole design.
- **It targets modest hardware.** The reference machine is a laptop whose GPU exposes no ray-tracing
  extensions at all.

## What this project is explicitly not attempting

- **Not a connectome.** No claim is made that anything here resembles a real nervous system.
- **Not a benchmark.** There are no tasks, no rewards, no scores and no leaderboard.
- **Not neuroscience.** No hypothesis about real animals is being tested. The biology in
  [PERCEPTION.md](PERCEPTION.md) is there to justify buffer sizes, and for no other reason.
- **Not a brain.** The renderer is the stage. What the actors do is somebody else's repository, and
  deliberately so — see [AGENT_INTERFACE.md](AGENT_INTERFACE.md).

What is left, after all those disclaimers, is a small honest world that runs on a cheap GPU and
lets people plug strange little minds into it. That is enough.
