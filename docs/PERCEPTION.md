# Perception

What the world renders into a creature's senses, why those sensor buffers are deliberately tiny,
and what the biology actually says.

## Scope

TronGrid Lite is the **stage, not the actor**. It renders senses; it does not think. An agent
loads as a shared-library plugin (DLL on Windows, SO on Linux), receives sensor buffers, and
returns motor intent. What happens in between is entirely the plugin's business — the world is
**agnostic about how any agent works inside**, and deliberately so. Nothing in this repository
models cognition, learning or behaviour, and nothing here should.

So this document is not about animal minds. It is about **the shape and size of the buffers the
renderer fills**, and the biology is here for one reason only: to justify those sizes. The
single most consequential number in the project is the one nobody thinks to question — **how
many samples does a sensor get?** Answer it with a display resolution and the renderer becomes
expensive, the simulation becomes small, and the sensor becomes a camera on legs. Answer it with
biology and the opposite happens.

The short version: **animal eyes resolve far less than 800×600, and most of the presets below
resolve less than a webcam thumbnail.** Since primary ray count scales with sample count, the
biologically honest choice is also the one that lets a dozen creatures each get their own
ray-traced view, every frame, on a modest GPU. Biology and performance point the same way. That
is not a happy accident — it is the reason animals are built this way too.

## How this document handles numbers

Every figure below comes from published measurements, and every translation from biology into
render terms is flagged as a translation. **No cited paper reports an animal's vision in
pixels** — the biology is theirs, the pixel equivalents are ours.

Two conventions are used throughout and never mixed:

- **Nyquist.** Resolving *f* cycles per degree needs 2 samples per degree: `px/deg = 2 × cyc/deg`.
  A figure in cycles per degree is never quoted as though it were pixels per degree.
- **Solid angle.** A whole sphere is 4π sr = 41,253 deg². Field coverage is computed as
  `Ω = Δaz(rad) × [sin(el_top) − sin(el_bot)]`, never as a naive `azimuth × elevation` product —
  that product frequently exceeds the entire sphere, which is how you know it is wrong.

Where the biology does not translate cleanly, this document says so instead of inventing a
number. Section [Things this document declines to quantify](#things-this-document-declines-to-quantify)
lists every such case.

## Sensor presets

The world offers a handful of sensor shapes, named after the animals whose measurements set
their size. A preset is a **render specification** — sample count, sample directions, acceptance
angles, channel count, quantisation — and nothing more. Which preset an agent asks for, and what
it does with the buffer, is the plugin's affair.

The names run from the simplest animal upward, and the sizes are startlingly small at the bottom.

| Preset | Recommended sensor | Channels | Primary rays per creature per frame | Biological anchor |
|------|--------------------|----------|-------------------------------------|-------------------|
| `elegans` | 1×2 body-referenced strip — *not* a view of the world | 1 (UV-weighted scalar) | **2** (8–16 with jitter) | 2 demonstrated spatial zones; no eyes, no opsins, no optics |
| `insect-min` | ~1,000–1,500 direction samples, non-uniform, both eyes | 1–3 | **1,000–1,500** | Ant 420–590 ommatidia per eye; *Drosophila* ~750 |
| `insect-mid` | ~9,450 direction samples (~69×69 per eye equivalent) | 3 | **9,450** | 4,725 ommatidia per eye (microCT) |
| `insect-high` | ~57,000 samples, foveated as a great-circle band | 3 | **57,000** | *Anax junius* ~28,000–30,000 ommatidia per eye |
| `rodent` | One ~166×166-equivalent panoramic buffer, uniform, no fovea | 1–3 | **17,600–27,500** | 0.4–0.5 cyc/deg acuity across an 8.38 sr field |
| `macropod` | 256×256 forward plus a coarse peripheral band | 3 | **65,536** | Tammar wallaby 4.8 cyc/deg behavioural |

For scale, a 640×480 spectator window is 307,200 pixels. **Every preset below the last is cheaper
than a single spectator frame, and the first three presets are cheaper by two to three orders of
magnitude.** Twenty simultaneous fruit-fly-class creatures cost about 30,000 primary rays per
frame — a tenth of one 640×480 image.

### Preset `elegans`: there is no image

This is the preset where the honest answer is *do not build a framebuffer at all*.

- **No eyes, no opsin genes, no lens or pigment cup.** Without an optical element mapping
  directions onto separate sensors, no scene can be reconstructed at any resolution.
- **302 neurons** in total (282 somatic plus 20 pharyngeal), of which about 60 are ciliated
  sensory neurons across every modality.
- **Spatial resolution is at most two body-referenced zones.** Illuminating the head drove
  reversal in 48 of 50 trials; illuminating the tail drove forward movement in 50 of 50. That is
  the only demonstrated spatial discrimination in the literature — and the stimulus spot was
  0.9 mm on a 1 mm animal, so two zones is an upper bound imposed by the experiment rather than
  a measured limit.
- **Roughly 8 to 14 photosensitive cells**, largely redundant and clustered in the head amphids,
  though photoreception is not head-exclusive — which is what underwrites the second zone.
- **One spectral channel, peaking outside human vision.** Peak response at 350 ± 25 nm (UV-A),
  strong at 435–470 nm, very little above 545 nm. Keep the blue channel; clamp to zero in the
  green and above.
- **Dynamic range of roughly 1.2 log units of usable graded response.** Ordinary room light is
  *sub-threshold* — about 100× weaker than the minimum that evokes a response. Treat the signal
  as 4 bits or fewer, not as an 8-bit value.

**Render prescription.** Cast two rays, one from the head segment and one from the tail, return
one UV-weighted scalar each, quantise coarsely, and require the creature to move over time to
infer anything directional. Total observation: **two to four scalars per timestep.**

### Presets `insect-min` / `insect-mid` / `insect-high`: a sample list, not a raster

**An ommatidium is not a pixel.** It is one light-integrating sample that averages everything
within its acceptance angle, in a curved, non-uniform, hexagonally packed array. The correct
data structure is a list of `(direction vector, acceptance angle)` per sample — not a rectangle.

| Species | Ommatidia per eye | Interommatidial angle Δφ | Acuity | Equivalent per eye | Both eyes |
|---------|-------------------|--------------------------|--------|--------------------|-----------|
| Desert ant *M. bagoti* | 420–590 | 3.7° mean | 0.135 cyc/deg | ~22×22 | ~32×32 (~1,000) |
| *Drosophila melanogaster* | ~750 | 4.5° mean, 3.4° frontal | 0.11 cyc/deg | ~27×27 | ~39×39 (~1,500) |
| Honeybee worker | 4,725 | 1.33° median | 0.24–0.38 cyc/deg | ~69×69 | ~97×97 (~9,450) |
| Dragonfly *Anax junius* | ~28,000–30,000 | 0.24° in the dorsal acute zone; ~0.9° whole eye | 2.08 cyc/deg in the acute zone | ~169×169 | ~240×240 (~57,344) |

The relationship between the acceptance angle Δρ and the sample spacing Δφ decides whether a
faithful render is blurry or aliased, and the two cases are genuinely different:

- **Δρ > Δφ** — acceptance cones overlap, so the image is **blurred**. *Drosophila* sits here
  (Δρ/Δφ ≈ 1.7–2.1): blur with a kernel about twice the sample spacing.
- **Δρ < Δφ** — the array **aliases**. The desert ant sits here (ratio 0.8): sharp samples,
  spatially undersampled. The literature notes such eyes "undersample the visual scene but
  provide high contrast".

**Motion changes the answer, and this is the most important caveat for a renderer.**
*Drosophila* behaviourally discriminates patterns of 1.16° wavelength despite a 4.5°
interommatidial angle — roughly four times finer than the lattice alone permits — through
microsaccadic sampling. A moving compound eye extracts more than its instantaneous sample count
implies, exactly like temporal supersampling.

Insect fields of view are near-panoramic, not rectangular: the honeybee's monocular field is
close to hemispherical, and both eyes together approach a full sphere with only 29° of frontal
binocular overlap. Do not render an insect through a perspective frustum.

### Preset `rodent`: uniform, wide and tiny

**The mouse has no fovea.** Its entire retina resembles the *peripheral* retina of a primate.
Model it as a uniform-resolution wide-angle buffer: no eccentricity falloff, no gaze-contingent
level of detail. Two caveats that do not justify a fovea: spectral response varies with
elevation (ventral UV-dominant, dorsal M-opsin dominant), and there is weak topography around a
slight area centralis.

- **Acuity: 0.5 cyc/deg** by visual water task, **~0.4 cyc/deg** by virtual optomotor — both
  defensible, differing by 36% in pixel density.
- **Field of view:** about 140° per eye in both axes, ~240° total azimuth, over 200° vertical.
  Converted properly that is **8.38 sr = 27,500 deg² = 67% of the whole sphere**. The naive
  240 × 200 = 48,000 deg² is provably wrong: it exceeds the sphere.
- **Whole-field budget: 17,600–27,500 samples** — a ~133×133 to ~166×166 image wrapped around
  two thirds of a sphere. That is **0.018–0.028 megapixels**.
- **Independent anatomical cross-check:** ~44,860 optic nerve axons per retina gives an
  anatomical Nyquist ceiling near 0.86 cyc/deg against 0.5 cyc/deg measured behaviourally —
  agreement within a factor of 1.7, which is the expected direction since only a subset of
  ganglion cell types carry fine spatial detail.

Do not cite "20/2000 vision" for mice; it is a conversion artefact with no stated basis. Cite
0.5 cyc/deg and let readers convert.

### Preset `macropod`: a band, not a disc

Macropods have a **horizontal visual streak with an embedded area centralis** — a wide, short
high-resolution strip suiting a ground-dwelling grazer scanning the horizon, not a small
circular inset.

- **Tammar wallaby**, the only macropod with real acuity measurements: **4.8 cyc/deg**
  behavioural, ~6 cyc/deg anatomical ceiling, ~2.7 cyc/deg by evoked potential. Contrast
  sensitivity peaks at only ~0.15 cyc/deg — this is a very low-frequency-tuned visual system.
- **Quokka cone anatomy is solid** even though its acuity is not: M/LWS cones peak at
  27,000/mm², SWS at 2,900/mm², with λmax 502 nm (MWS) and 538 nm (LWS). Three cone classes, so
  potentially trichromatic — but the blue channel is roughly ten times sparser than red/green
  and concentrated dorso-temporally. Against a human foveal peak near 199,000 cones/mm² the
  deficit is about 7× in area, 2.6× in linear sampling pitch, before accounting for the much
  smaller eye.
- Quokkas are nocturnal to crepuscular, so the render budget belongs in **dynamic range and a
  low light floor, not in resolution or colour fidelity**.

**Recommended target: 256×256 forward plus a coarse peripheral band at about a third of that
rate.** A graded specification is more honest than one number: roughly 10 px/deg at the area
centralis, 7–8 px/deg along the streak, 3–4 px/deg elsewhere.

## The human comparison, and why 4K exists

| Quantity | Value |
|----------|-------|
| 20/20 letter acuity | 1 arcmin MAR = 30 cyc/deg = **60 px/deg** |
| Foveal cone Nyquist | ~60 cyc/deg = **120 px/deg** (individual range 50–85 cyc/deg) |
| Modern measured foveal limit | **94 px/deg** achromatic median, 53 px/deg yellow-violet |
| Fovea extent | foveola ~1°, fovea centralis ~5°, macula ~18° |
| Central 2° at 120 px/deg | 240×240 = **0.058 megapixels** |
| Photoreceptors | ~92M rods, ~4.6M cones |
| Optic nerve | **~1.2M axons per eye** — about 80:1 compression |
| Instantaneous foveated snapshot | **~0.9–2 megapixels per eye** |

The sharpest patch of human vision — the central two degrees — is a **240×240 image**. The
often-quoted 576-megapixel figure for the eye is arithmetically correct but answers a different
question: it is what a uniform render would need so as never to be the limiting factor for a
*freely scanning* eye. Its own author is explicit that "at any one moment, you actually do not
perceive that many pixels".

**So why does 4K exist?** 3840 × 2160 is 8.29 megapixels, while a human resolves 1–2 megapixels
per eye at any instant. The answer is that **the fovea moves and the display cannot predict
where**. Any pixel may be foveated after the next saccade, so all of them must be
foveal-quality even though only a fifth of them are being resolved at once. 4K is insurance
against gaze.

**A simulated creature presents no such problem.** TronGrid Lite knows exactly where every
creature's sensors point, because it computes them. There is no unpredictable gaze to insure
against, and for creatures with no fovea the concept does not even apply. This asymmetry is why
tiny sensors are *correct* here rather than merely cheap.

## What embodied-AI practice actually feeds to networks

A decade of reinforcement-learning and robotics research converged on observations far below any
display resolution.

| System | Observation | Pixels per frame |
|--------|-------------|------------------|
| VizDoom (original) | 60×45×3 RGB | 2,700 |
| Procgen (all 16 environments) | 64×64×3 RGB | 4,096 |
| MineRL baselines | 64×64 greyscale | 4,096 |
| DeepMind Lab / DMLab-30 (IMPALA) | 96×72×3 RGB | 6,912 |
| Atari / ALE (DQN) | 84×84×4 greyscale stack | 7,056 |
| MineDojo / MineCLIP | 160×256×3 | 40,960 |
| OpenVLA | 224×224 | 50,176 |
| Habitat PointGoal, Octo | 256×256 | 65,536 |
| RT-1 | 300×300, compressed to **8 tokens per frame** | 90,000 |

The median of these is about 9,216 pixels; the mode is 64×64. Three published null results
license the low end directly:

1. **DD-PPO (Habitat)** average-pools 256×256 to an effective 128×128 — a 4× pixel reduction —
   and reports "no impact on performance", reaching 0.969 SPL on PointGoal navigation.
2. **OpenVLA** trained at both 224×224 and 384×384 and "found no performance difference in our
   evaluations, while the latter takes 3× longer to train". It shipped the smaller one.
3. **RT-1** compresses a 300×300 input to eight tokens per image; the policy transformer never
   sees a spatial grid at all.

**The counter-evidence is architectural and worth respecting.** Recent work argues the
low-resolution convention was inherited from early benchmarks rather than designed, and shows
that resolutions from 48×48 up to 112×112 *do* help — but only once the encoder is
resolution-independent (global average pooling instead of a flatten). Note where that tops out:
**112×112 is still only 12,544 pixels.** The finding is not "you need big renders", it is
"within the tiny regime, do not pick 64×64 reflexively, and do not use a flatten-based encoder".

## The acoustic side

Sound is where the fidelity-follows-acuity argument is strongest: hearing is orders of magnitude
coarser than vision, and acoustic simulation tolerates error that would be catastrophic in a
renderer.

- **Ray counts.** A canonical worked example uses 5,000 rays with 4 ms energy bins across 7
  octave bands — over a full sphere that is about 2.9° between rays. Published room-acoustics
  studies commonly use 5,000–30,000. Audio-rate accuracy would need hundreds of thousands, but
  **accepting millisecond bins instead of microsecond ones cuts the requirement by at least 40×**.
- **Update rate.** A moving-source auralization matched offline quality at an **8.6 Hz average
  solve rate**, provided propagation delay was interpolated between solves — the expensive solve
  runs at ~9 Hz while the output stream stays at audio rate. Interactive engines update a full
  sound field in 7–14 ms on desktop. Against 60–90 Hz visuals, the acoustic solve can run once
  per seven to ten frames and remain transparent.
- **Why the tolerance exists.** Three properties of the *simulation*: energy histograms are 44–176×
  coarser than the audio sample period and their fine structure is synthesised as noise; late
  reverberation needs only **12–24 spatial directions** (about 41° apart — 24 samples for the entire
  diffuse tail); and frequency is handled in 6–9 octave bands.

  A fourth mechanism is often cited alongside these and is deliberately left out: the precedence
  effect, by which reflections arriving within a few milliseconds are fused and the leading
  direction weighted roughly 4:1. It is real, but it is a property of an auditory *system* rather
  than of the world, and this repository does not get to size its own fidelity on assumptions about
  how a listener will process what it receives — that is the brains' business, in their own
  repositories. It is also specifically a mammalian result, and there is no reason to expect it of a
  moth with two receptor cells or a nematode sensing pressure gradients through its skin. The three
  simulation-side mechanisms carry the conclusion on their own.
- **Angular acuity of hearing** is far coarser than vision. Human minimum audible angle is about
  1.1° frontally against a foveal 1 arcmin — **66× coarser linearly, ~4,400× in solid angle**.
  Mouse localisation acuity is around 31° in the horizontal plane, and roughly 81° in the median
  plane — the vertical figure being the one measured case of the caveat this document already
  records, that every minimum audible angle quoted here is azimuthal and median-plane acuity is
  several times worse. Motion makes it worse still: the minimum audible *movement* angle rises from
  8.8° at 10°/s to 20.2° at 180°/s.

One honest caveat in the other direction: an emitted acoustic ray is not a pixel. It is traced
through hundreds of reflections across a two-to-three-second impulse response, so a 30,000-ray
solve is millions of ray-segments of work.

## The senses that are not eyes

Vision dominates this document because it dominates the render budget, but it is not the only
modality the world hands over. The full list lives in
[AGENT_INTERFACE.md](AGENT_INTERFACE.md#the-senses-at-a-glance); what follows is why the
non-visual ones are shaped as they are.

**Vestibular sensing costs three floats and prevents a specific failure.** Otolith organs sense
linear acceleration, semicircular canals sense rotation, and crucially the otolith cannot separate
gravity from acceleration — the two arrive as a single specific force, which is exactly why tilting
a person in the dark feels like accelerating forwards. The world reports the same conflated
quantity rather than a clean "down" vector, so a creature is fooled in the same way an animal is.

That matters more here than in most worlds. **Every surface is a perfect mirror**, so the reflected
floor is geometrically indistinguishable from real space below it. A creature navigating on vision
alone has no way to know which of the two halves it can fall through. Animals resolve exactly this
ambiguity with inertial sensing, and the numbers are already sitting in the motion integrator.

**Thermoreception is one unresolved radiance sample, and that is biologically defensible rather
than a shortcut.** The pit organs of crotaline snakes are pinhole structures with no lens, so the
image falling on the membrane is severely blurred; the animal's much sharper behavioural
performance is attributed to neural reconstruction rather than to the optics. Coarse radiance
sensing is a real modality in real animals. No specific angular figure is quoted here, in keeping
with the rest of this document — the point stands qualitatively, and it costs a single ray per
creature because the tracer already computes incoming radiance for a living.

It also gives the smallest presets something to do. A creature with two scalar photoreceptors and a
warmth sense can perform genuine taxis long before it can resolve an edge, which is the correct
order in which to climb the ladder.

**Proprioception and vestibular sensing are deliberately separate fields.** The first is what the
body's actuators report about themselves, the second is what inertia reports about the body. They
agree while a creature drives itself and disagree the moment it is pushed, slides or falls — and
that disagreement is information a brain can use.

## Design rules

These follow from everything above. They are binding on the renderer.

1. **Render natively small; never downscale a big render.** The cost argument only works if the
   small buffer is what the GPU actually traces. A downsampled image is also the *wrong* image:
   box-filtered downsampling is antialiased, whereas a native small render aliases — and for
   ant-class eyes, aliasing is the biologically correct result.
2. **Sensor targets are structurally separate from the spectator window.** The spectator window
   is sized for a human and their unpredictable gaze. Creature sensors are simulation state.
   They share the scene, the BVH and the materials, and nothing else: no shared resolution,
   aspect ratio, channel count or post-processing. A creature's sensor is not a camera.
3. **Per-creature sensor descriptors.** Each creature owns its sample count,
   sample-direction list, per-sample acceptance angle, channel count and quantisation. Twenty
   C. elegans cost 40 rays; twenty fruit flies cost 30,000. These are different data structures,
   not one structure with a slider, and the budget arithmetic is per creature.
4. **For the small presets the sensor is a sample list, not a raster.** Store direction and
   acceptance angle per sample. Interommatidial angle varies severalfold across a single eye, so
   a single "equivalent resolution" is a communication device for this document, never the
   implementation.
5. **Low channel counts, and spectral response is not RGB.** C. elegans gets one scalar weighted
   to 350–440 nm, zeroed above 545 nm, coarsely quantised. Insects get one to three. Quokka-class
   gets three, with the blue channel sparser and spatially biased. Uniform RGB is the exception
   in this document, not the default.
6. **Blur, aliasing and noise are the creature's problem to cope with, not bugs to fix.** A
   faithful *Drosophila* render blurs with a kernel twice the sample spacing; a faithful ant
   render aliases; a C. elegans sensor is a single noisy scalar that cannot resolve direction at
   all. If an agent cannot cope, that is a finding about the agent. Do not add antialiasing,
   temporal accumulation or denoising to make an agent's life easier unless the biology has an
   equivalent.
7. **Model motion as a resolution mechanism, not just as animation.** A moving compound eye
   resolves about four times finer than its lattice permits; C. elegans must move to infer
   direction at all; rodents rely on motion parallax rather than stereo. The temporal dimension
   is where low-resolution creatures recover capability, and it is free in a real-time
   simulator. Do not compensate for a small sensor by enlarging it — let the creature move.
8. **Foveate only where the biology foveates, and get the shape right.** Mouse and rat: no
   fovea, uniform buffer, gaze-contingent detail buys nothing. Dragonfly: a great-circle band
   from lateral through dorsal to lateral. Macropods: a horizontal streak with an embedded area
   centralis. There are no human creatures here, so the small central disc is never the model.
9. **Render fields as spheres, not frusta.** Mouse fields cover 67% of the sphere and bee fields
   approach all of it. Compute solid angle properly and sanity-check every field figure against
   41,253 deg²; if a naive azimuth × elevation product exceeds that, the geometry is wrong.
10. **Acoustic tolerances apply to audio only.** A few thousand rays, millisecond energy bins,
    6–9 octave bands, a ~10 Hz solve with per-block interpolation, 12–24 directions for the
    diffuse tail. These tolerances must not leak into the visual path: audio earns them through
    millisecond energy integration and the coarseness of the histogram itself, and vision has no
    equivalent.
11. **Publish the assumption alongside every derived number.** Nearly every error caught while
    verifying this document was a translation error rather than a biology error: peak acuity
    applied whole-field, cycles per degree quoted as pixels per degree, per-eye counts summed as
    totals, an interquartile range read as an interval. State the measured figure, the conversion
    and the assumption, so the next person can check the arithmetic instead of inheriting it.
12. **Where the biology does not translate, say so; do not invent a pixel.**

## Things this document declines to quantify

- C. elegans resolution beyond two body-referenced zones — the stimulus could not have resolved
  more, so two is an experimental upper bound, not a measurement.
- The number of discriminable intensity levels for any invertebrate. No retrieved paper measures
  bit depth.
- Quokka-specific acuity. A figure of ~4 cyc/deg circulates in a comparative dataset but its
  primary provenance could not be traced; the tammar wallaby is a proxy from a different species.
- Quokka eye axial length, so no anatomical acuity can be computed for it.
- Total rat field of view in degrees.
- Elevation-resolved acuity for the mouse, and elevation acuity for every auditory figure quoted
  here — all minimum-audible-angle values are azimuthal, and median-plane acuity is several times
  worse.

## Citations

### C. elegans photoreception

- Ward A, Liu J, Feng Z, Xu XZS (2008), "Light-sensitive neurons and channels mediate phototaxis
  in C. elegans", *Nature Neuroscience* 11(8):916–922 —
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC2652401/>
- Edwards SL et al. (2008), "A Novel Molecular Solution for Ultraviolet Light Detection in
  Caenorhabditis elegans", *PLOS Biology* 6(8):e198 —
  <https://journals.plos.org/plosbiology/article?id=10.1371/journal.pbio.0060198>
- Ghosh DD, Lee D, Jin X, Horvitz HR, Nitabach MN (2021), "C. elegans discriminates colors to
  guide foraging", *Science* 371(6533):1059–1063 —
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC8554940/>
- Gong J et al. (2016), "The C. elegans Taste Receptor Homolog LITE-1 Is a Photoreceptor",
  *Cell* 167(5):1252–1263 — <https://pmc.ncbi.nlm.nih.gov/articles/PMC5388352/>
- White JG, Southgate E, Thomson JN, Brenner S (1986), "The structure of the nervous system of
  the nematode Caenorhabditis elegans", *Phil Trans R Soc Lond B* 314:1–340 —
  <https://pubmed.ncbi.nlm.nih.gov/22462104/>

### Insect compound eyes

- Land MF (1997), "Visual acuity in insects", *Annual Review of Entomology* 42:147–177 —
  <https://pubmed.ncbi.nlm.nih.gov/15012311/>
- Millward et al. (2022), "CompoundRay, an open-source tool for high-speed and high-fidelity
  rendering of compound eyes", *eLife* — <https://pmc.ncbi.nlm.nih.gov/articles/PMC9605689/>
- Juusola M et al. (2017), "Microsaccadic sampling of moving image information provides
  Drosophila hyperacute vision", *eLife* 6:e26117 — <https://elifesciences.org/articles/26117>
- Rigosi E, Wiederman SD, O'Carroll DC (2017), "Visual acuity of the honey bee retina and the
  limits for feature detection", *Scientific Reports* 7:45972 —
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC5382694/>
- Schwarz S, Narendra A, Zeil J (2011), "The properties of the visual system in the Australian
  desert ant Melophorus bagoti", *Arthropod Structure & Development* 40(2):128–134 —
  <https://pubmed.ncbi.nlm.nih.gov/21044895/>
- Sherk TE (1978), "Development of the compound eyes of dragonflies (Odonata). III. Adult
  compound eyes", *Journal of Experimental Zoology* 203(1):61–80 —
  <https://pubmed.ncbi.nlm.nih.gov/624923/>
- Seidl R, Kaiser W (1981), "Visual field size, binocular domain and the ommatidial array of the
  compound eyes in worker honey bees", *Journal of Comparative Physiology* 143:17–26

### Senses that are not eyes

- Clark RW, Bakken GS, Reed EJ, Soni A (2022), "Pit viper thermography: the pit organ used by
  crotaline snakes to detect thermal contrast has poor spatial resolution", *Journal of
  Experimental Biology* 225(24):jeb244478 — the title is the claim —
  <https://journals.biologists.com/jeb/article/225/24/jeb244478/286197/>
- Sichert AB, Friedel P, van Hemmen JL (2006), "Snake's Perspective on Heat: Reconstruction of
  Input Using an Imperfect Detection System", *Physical Review Letters* 97:068105 — the wide-
  aperture pinhole optics of the pit organ, and the neural reconstruction that sharpens them —
  <https://journals.aps.org/prl/abstract/10.1103/PhysRevLett.97.068105>
- Angelaki DE, Cullen KE (2008), "Vestibular System: The Many Facets of a Multimodal Sense",
  *Annual Review of Neuroscience* 31:125–150 — semicircular canal and otolith signals, and the
  gravity versus acceleration ambiguity — <https://pubmed.ncbi.nlm.nih.gov/18338968/>
- Bargmann CI (2006), "Chemosensation in C. elegans", *WormBook* — more than 5% of the animal's
  genes are devoted to recognising environmental chemicals, which is why the simplest preset on
  this ladder is named after a chemotactic animal — <https://www.ncbi.nlm.nih.gov/books/NBK19746/>

### Small mammals

- Prusky GT, West PW, Douglas RM (2000), "Behavioral assessment of visual acuity in mice and
  rats", *Vision Research* 40(16):2201–2209 — <https://pubmed.ncbi.nlm.nih.gov/10878281/>
- Prusky GT, Alam NM, Beekman S, Douglas RM (2004), "Rapid quantification of adult and
  developing mouse spatial vision using a virtual optomotor system", *IOVS* 45(12):4611–4616 —
  <https://pubmed.ncbi.nlm.nih.gov/15557474/>
- Huberman AD, Niell CM (2011), "What can mice tell us about how vision works?", *Trends in
  Neurosciences* 34(9):464–473 — <https://pmc.ncbi.nlm.nih.gov/articles/PMC3371366/>
- Jeon CJ, Strettoi E, Masland RH (1998), "The Major Cell Populations of the Mouse Retina",
  *Journal of Neuroscience* 18(21):8936–8946 —
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC6793518/>
- Lauer AM, Slee SJ, May BJ (2011), "Acoustic Basis of Directional Acuity in Laboratory Mice",
  *Journal of the Association for Research in Otolaryngology* 12(5):633–645 —
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC3173556> — the 31° horizontal and 80.7° median-plane
  minimum audible angles
- Heffner HE, Heffner RS (2016), "The Evolution of Mammalian Sound Localization", *Acoustics Today*
  12(1):20–27 —
  <https://acousticstoday.org/wp-content/uploads/2016/01/The-Evolution-of-Mammalian-Sound-Localization.pdf>
  — 33° for mice among 39 mammal species, agreeing independently with Lauer et al.
- Heffner HE, Heffner RS (2007), "Hearing Ranges of Laboratory Animals", *Journal of the American
  Association for Laboratory Animal Science* 46(1):20–22 —
  <https://www.vogelabwehr.at/images/PDF/hearing-range-animals.pdf> — the mouse's 2.3–85.5 kHz
  hearing range at 60 dB SPL

### Marsupials

- Hemmi JM, Mark RF (1998), "Visual acuity, contrast sensitivity and retinal magnification in a
  marsupial, the tammar wallaby (Macropus eugenii)", *Journal of Comparative Physiology A*
  183:379–387 — <https://pubmed.ncbi.nlm.nih.gov/9763704/>
- Arrese CA et al. (2005), "Cone topography and spectral sensitivity in two potentially
  trichromatic marsupials, the quokka (Setonix brachyurus) and quenda (Isoodon obesulus)",
  *Proceedings of the Royal Society B* 272(1565):791–796 —
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC1599861/>
- Harman AM, Moore S (1999), "Number of neurons in the retinal ganglion cell layer of the quokka
  wallaby do not change throughout life", *Anatomical Record* 256(1):78–83 —
  <https://pubmed.ncbi.nlm.nih.gov/10456988/>
- Veilleux CC, Kirk EC (2014), "Visual Acuity in Mammals: Effects of Eye Size and Ecology",
  *Brain, Behavior and Evolution* 83(1):43–53 —
  <https://karger.com/bbe/article/83/1/43/326295/>

### Human vision

- Kalloniatis M, Luu C, "Visual Acuity", *Webvision* —
  <https://www.ncbi.nlm.nih.gov/books/NBK11509/>
- Curcio CA, Sloan KR, Kalina RE, Hendrickson AE (1990), "Human photoreceptor topography",
  *Journal of Comparative Neurology* 292(4):497–523 —
  <https://pubmed.ncbi.nlm.nih.gov/2324310/>
- Kolb H, "Facts and Figures Concerning the Human Retina", *Webvision* —
  <https://www.ncbi.nlm.nih.gov/books/NBK11556/>
- Yellott JI (1990), "The Photoreceptor Mosaic as an Image Sampling Device", in *Advances in
  Photoreception* — <https://www.ncbi.nlm.nih.gov/books/NBK235550/>
