# Acoustics: Background

The evidence behind [ACOUSTICS.md](../ACOUSTICS.md), separated from it so that the design document can
be read as a design document. Nothing here is a decision; the decisions and the build proposal live
next door, and each of them points back at the section here that argues for it.

Every citation was checked against its source before it was written down. Where a work is cited for
a specific number, that number was read out of the work rather than remembered.

---

## What the Literature Settled On, and Which Half Applies Here

Geometrical acoustics is a small-wavelength approximation: it assumes sound travels in straight lines
like light and discards diffraction, interference and phase. Savioja and Svensson's 2015 review is
the canonical statement, Savioja and Xiang's open-access companion puts it in one sentence, and
Masović's lecture notes give the criterion its sharpest form — surfaces must be acoustically large,
meaning the Helmholtz number `kL` is much greater than one.

The field's mature architecture splits the impulse response in time: image sources for the early
reflections, stochastic ray tracing for the late reverberant tail. Vorländer's 1989 combined
algorithm is the canonical statement of the split, and Rindel gives the clearest account of why both
halves are necessary. Image sources are exact — Allen and Berkley for rectangular rooms, Borish for
arbitrary polyhedra — but the candidate count grows as `n(n-1)^(i-1)`, and Rindel's worked example
reaches roughly 10^19 candidates at order 13 for 2,500 valid arrivals. Ray tracing is cheap but
noisy, needs a volumetric receiver, and needs enough rays to *discover* each surface at all.

**TronGrid Lite needs the first half and cannot produce the second.** That is the largest single
simplification available in this phase, and the terraced relief does not change it.

### What survives

- **Geometrical acoustics as the method**, with its validity stated as a frequency band rather than
  assumed.
- **The image source method** for paths that can be enumerated, validated by shadow rays the BVH
  already answers.
- **The energy histogram** — time bins by frequency bands — as the output structure. Krokstad, Strøm
  and Sørsdal introduced ray tracing to room acoustics in 1968 and the accumulator has barely changed
  in principle since.
- **Air absorption**, from ISO 9613-1 with ISO 9613-2's tabulated octave-band coefficients, and
  Lawrence and Simmons for the ultrasonic range. This turns out to be the only strongly
  frequency-dependent term in the entire model.
- **Absorption coefficients** and, more usefully, the published honesty about how uncertain they are.
- **The published ray counts and solve rates** of shipping systems, which are far smaller than a
  renderer's intuition suggests.

### What does not

Everything whose definition contains the word *enclosure*:

- **RT60, Sabine and Eyring.** ISO 3382-1 defines reverberation time as the duration for the
  space-averaged energy density *in an enclosure* to fall 60 dB. Sabine's `T60 ≈ 0.16 V / A` has `V`
  unbounded here and `A` finite; the formula does not become inaccurate, it becomes meaningless.
  Masović names this failure mode precisely — a large absorbing floor against negligible wall area —
  and this scene is that mode taken to its limit.
- **The diffuse field, modal behaviour and the Schroeder frequency.** With no ceiling and no walls the
  field can never become diffuse, because energy leaves rather than circulating.
- **Late reverberation synthesis**, mean-free-path estimation, transition order, and the 40 to 100
  diffuse reflection orders Schissler and Manocha need for one to two seconds of tail. Schissler,
  Mehra and Manocha note that in outdoor scenes most rays escape after the fourth or fifth bounce;
  this world is more extreme than any outdoor benchmark in the literature.
- **Receiver spheres.** They exist to make a stochastic ray count converge in a closed room. A 0.5 m
  sphere smears arrival times by 1.5 ms, which is larger than the histogram bin this document
  proposes, and this scene is sparse enough to afford a point receiver.
- **Every wave method**, for reasons given under [What Phase 5 Rejects](../ACOUSTICS.md#what-phase-5-rejects).

### The wavelength ledger

The single cheapest way to say what the acoustic renderer can and cannot mean is to state, for each
feature of the world, the frequency at which one wavelength equals it. Below that frequency the
feature is sub-wavelength and a ray-traced shadow behind it is fiction; well above it, the ray model
is doing honest work.

| Feature | Size | One wavelength at |
|---------|------|-------------------|
| Neon tube lift above the floor | 0.01 m | 34.3 kHz |
| Neon tube width | 0.05 m | 6.86 kHz |
| Glass slab thickness | 0.7 m | 490 Hz |
| **Terrace step height** | **0.833 m** | **412 Hz** |
| Pillar cross-section | 0.9 m | 381 Hz |
| Glowing column cross-section | 1.4 m | 245 Hz |
| Grid cell | 2.0 m | 172 Hz |
| Glass slab width | 6.0 m | 57 Hz |
| Largest single acoustic plane — one terrace level, median run | 14 m | 24.5 Hz |
| Floor plan extent (no longer one plane) | 128 m | 2.7 Hz |

Read down that table and the world sorts itself into three classes. The **terrace levels are valid at
every audible frequency** and are the only surfaces of which that is true — and note that this is now
a statement about six stepped planes rather than about one 128 m sheet, because the relief means
there is no single floor plane any more. The **terrace steps, pillars, slabs and column are valid
above roughly 500 Hz to 1 kHz** and progressively fictitious below it. The **neon tubes are
acoustically nothing below about 7 kHz**: a 5 cm ribbon lying 1 cm above a hard plane is not a
scatterer, it is a paint job on the floor.

The honest summary for anyone reading this later: **geometrical acoustics is sound in this scene
above about 1 kHz, defensible from 500 Hz to 1 kHz, and progressively fictitious below 500 Hz, with
the terrace levels themselves the sole exception.** That is a reason to state the valid band and to
choose source frequencies inside it, not a reason to abandon rays.

### Creature scale makes diffraction *less* important, not more

The intuition is that centimetre-scale creatures hearing wavelengths comparable with the obstacles
should make diffraction matter more than in a human-scale game. The intuition is wrong, and the
reason is biological rather than convenient.

Small animals cannot afford low-frequency hearing. Heffner and Heffner make the mechanism explicit:
long wavelengths bend around a small head with little attenuation, so a mammal with a small head must
hear frequencies high enough to be *shadowed* by its head before any interaural level difference
exists at all. Across 69 species the correlation between functional head size and the highest
frequency audible at 60 dB is `r = -0.79`. Puria and Rosowski restate it independently.
**High-frequency hearing in small mammals is an evolutionary escape from diffraction — and the
frequencies they escaped to are precisely the frequencies at which geometrical acoustics becomes
valid.**

Run it against a pillar of half-width 0.45 m:

| Frequency | Wavelength | Pillar widths | `ka` |
|-----------|------------|---------------|------|
| 100 Hz | 3.43 m | 0.26 | 0.8 |
| 1 kHz | 34.3 cm | 2.6 | 8.2 |
| 2.3 kHz (mouse floor) | 14.9 cm | 6.0 | 19.0 |
| 3 kHz (the hum) | 11.4 cm | 7.9 | 24.7 |
| 8 kHz | 4.3 cm | 21.0 | 66 |
| 85.5 kHz (mouse ceiling) | 4.0 mm | 224 | 705 |

A human attending to a 100 Hz component sees a quarter-wavelength obstacle: it is not there. A
mouse-class creature sees the same pillar as 6 to 224 wavelengths across across its entire hearing
range. That is the geometric-optics regime by any standard, and it is what makes "no diffraction in
Phase 5" a physics decision rather than a budget one.

Three caveats are not being hidden. First, the diffraction that matters most to a small animal is
around its own head, and this world does not model creature bodies as acoustic scatterers at all.
Second, the insect presets are not pressure receivers and a pressure-energy histogram computes a
quantity their ears do not measure — see [the roster](#the-creature-roster). Third, all of it is
conditional on the sources being high-frequency, which is exactly what
[the hum decision](../ACOUSTICS.md#the-humming-neon) is about.

---

---

## The Creature Roster

What each preset in [PERCEPTION.md](../PERCEPTION.md) can actually hear, and where the figures come
from. The sensor shapes derived from this are in [ACOUSTICS.md](../ACOUSTICS.md).

Every preset's acoustic specification, with its provenance stated. As in
[PERCEPTION.md](../PERCEPTION.md), the biology is theirs and the buffer sizes are ours.

| Preset | Anchor | Frequency range | Ears | Bands | Localisation the biology achieves (recorded for calibration; not a world parameter) | Of the 3 kHz hum it hears |
|--------|--------|-----------------|------|-------|--------------|---------------------------|
| `elegans` | *C. elegans* | 100 Hz – 5 kHz, pressure **gradient**, whole cuticle | 2 (head, tail) | 1 | none; must move | the fundamental only |
| `insect-min` | *Drosophila* / ant | near-field particle velocity, ~100–300 Hz, range mm to a few cm | 0 | — | — | nothing |
| `insect-mid` | Honeybee | Johnston's organ, likewise near-field | 0 | — | — | nothing |
| `insect-high` | Dragonfly | no evidence of hearing | 0 | — | — | nothing |
| `rodent` | House mouse | 2.3 – 85.5 kHz at 60 dB | 2 | 4 | 31° azimuth, 80.7° median plane | fundamental and every harmonic |
| `macropod` | Quoll + wallaby proxies | ~0.5 – 40 kHz, best 8–10 kHz | 2 | 4 | coarse; horizontal streak | fundamental plus harmonics to the 13th |

Six things in that table are worth the prose.

**C. elegans hears, and the sensor is the strip it already has.** Iliff and colleagues overturned the
assumption that a nematode is deaf: the animal responds to airborne sound from 100 Hz to 5 kHz with
thresholds reaching 50–60 dB SPL, transduced by FLP and PVD mechanosensory neurons through the whole
cuticle acting as a distributed eardrum. The 2024 follow-up sharpens it in a way that decides the
sensor shape: the worm responds to **pressure gradients** rather than to absolute level, and
selectively to localised sound. So the acoustic `elegans` sensor is not a spectrum and not a level —
it is a two-sample difference along the body axis, exactly mirroring the two-zone photoreceptive strip
already specified. Two "ears" at head and tail, one band, and the world does **not** compute the
difference: taking it is interpretation and belongs to the brain.

**Silence is a legitimate sensor specification.** Römer puts the independent evolution of insect ears
at probably more than twenty times, which is the strongest possible statement that hearing is not an
ancestral insect trait — deafness is the default. Worse for a ray tracer, the insect ears that do exist
in the modelled lineages detect the **wrong physical quantity**. Yorozu and colleagues showed the fruit
fly's antennal arista and Johnston's organ respond to small bi-directional displacements — near-field
air motion, not far-field pressure. Hoy states the consequence bluntly for mosquitoes: the ear is
insensitive to far-field pressure fluctuations, and the particle-velocity field falls off as the
**inverse cube**, so hearing works over millimetres to a few tens of centimetres. That is not a
sensitivity limit a louder source could overcome; it is the wrong field at any distance. An energy
histogram computes a quantity these sensors do not measure, and no amount of extra fidelity fixes it.
**The three insect presets get `ear_count = 0`,** and the document should say so as a positive
specification rather than an omission.

**One band and two thresholds is a functioning sense.** ter Hofstede and colleagues characterise the
noctuoid moth ear as exactly two receptor cells per ear, A1 and A2, both tuned to roughly 20–50 kHz
with A2's threshold about 20 dB above A1's, and state explicitly that "moths are tone-deaf" and cannot
discriminate frequency. Those two cells are not a two-band spectrum; they are a two-level intensity
scale on one broad band. An animal that detects, ranges and evades an actively hunting predator in
flight does it with one band. This anchors the bottom of the band-count ladder exactly as the
two-photoreceptor argument anchors the resolution ladder, and it is the strongest available answer to
anyone who assumes a creature needs a spectrum. No preset on the current roster is a bat-detector; if a
prey animal is ever added, this is its specification.

**The mouse's deafness below 2.3 kHz is a design constraint, not a detail.** Heffner and Heffner give
the house mouse a 60 dB range of 2.3 kHz to 85.5 kHz — the upper limit more than two octaves above the
human 17.6 kHz, the lower limit the highest of any common laboratory mammal, and a range of *good*
hearing spanning only 0.4 octaves against a cat's 6.6. A rodent creature genuinely cannot hear
low-frequency content in this world, and that is the same class of decision as clamping the `elegans`
visual response to zero above 545 nm.

**The macropod figures are proxies, and the chain is one step weaker than the vision proxy.** There is
no published audiogram for *Setonix brachyurus*, behavioural or otherwise, and none for the tammar
wallaby either — what exists for the tammar is Cone-Wesson, Hill and Liu's auditory brainstem response
study, and Heffner and Heffner warn explicitly that neural measures "often give estimates of hearing
sensitivity that diverge from what an animal can actually hear". So the range comes from the northern
quoll, a dasyurid rather than a macropod: about 0.5 to 40 kHz at 50 dB SPL with best frequency between
8 and 10 kHz. The strongest macropod data is Coles and Guppy's external-ear biophysics for the tammar:
a maximum on-axis pressure gain of 25–30 dB near 5 kHz, an acoustic axis above 2 kHz lying close to the
horizontal at natural ear positions, and binaural intensity differences exceeding 30 dB from the
interaction of the two monaural directivity patterns. **Nothing in that row comes from a quokka**, and
the document says so for the same reason PERCEPTION.md says it about acuity.

**Localisation acuity can be predicted from the visual anatomy already published.** This is the most
elegant result in the whole survey, because it links the acoustic section to the vision section rather
than sitting beside it. Across mammals, minimum audible angle does not track head size: 1–2° for humans
and elephants, 5° for cats, 12° for Norway rats, but 25° for horses and 30° for cattle despite large
heads. Having rejected body size, predator-prey status, diurnality and binocular field width, Heffner
and Heffner found that localisation acuity correlates with the width of the animal's **field of best
vision** at `r = 0.89`, and not at all with absolute visual acuity. Their interpretation is that the
primary function of sound localisation is to direct the eyes. So the `rodent` preset — explicitly no
fovea, uniform wide field — predicts poor acuity, and the measured mouse figure of 31–33° confirms it;
the `macropod` preset's horizontal visual streak predicts coarse localisation too. **The acoustic
angular resolution each preset's biology achieves follows from the retinal topography already cited,
and does not need researching independently** — which is a calibration check on the roster, not a
quantity the world computes.

That last point is already reflected in PERCEPTION.md, which gives the mouse 31° in the horizontal
plane and roughly 81° in the median plane. The primary sources are Lauer, Slee and May, who report an
average minimum audible angle of 31° for CBA/129 mice on a conditioned-suppression task and 80.7° in
the median plane, with Heffner and Heffner's comparative figure independently giving 33°.

---

## References

### Bioacoustics: what the creatures can hear

- Iliff AJ, Wang C, Ronan EA, Hake AE, Guo Y, Li X, Zhang X, Zheng M, Liu J, Grosh K, Duncan RK, Xu XZS
  (2021). "The nematode *C. elegans* senses airborne sound". *Neuron* 109(22):3633–3646.e7. DOI
  10.1016/j.neuron.2021.08.035 — <https://pmc.ncbi.nlm.nih.gov/articles/PMC8602785/>
- Wang C, Ronan EA, Iliff AJ, Al-Ebidi R, Kitsopoulos P, Grosh K, Liu J, Xu XZS (2024). "Characterization
  of auditory sensation in *C. elegans*". *Biophysics Reports* 10(6):351–363. DOI
  10.52601/bpr.2024.240027 — <https://pmc.ncbi.nlm.nih.gov/articles/PMC11693501/>
- Heffner HE, Heffner RS (2007). "Hearing Ranges of Laboratory Animals". *Journal of the American
  Association for Laboratory Animal Science* 46(1):20–22 —
  <https://www.vogelabwehr.at/images/PDF/hearing-range-animals.pdf>
- Heffner HE, Heffner RS (2016). "The Evolution of Mammalian Sound Localization". *Acoustics Today*
  12(1):20–27 —
  <https://acousticstoday.org/wp-content/uploads/2016/01/The-Evolution-of-Mammalian-Sound-Localization.pdf>
- Heffner RS, Heffner HE (2010). "Explaining High-Frequency Hearing". *The Anatomical Record*
  293(12):2080–2082. DOI 10.1002/ar.21292
- Puria S, Rosowski JJ (2024). "High frequency hearing: A uniquely mammalian trait for sound
  localization". *Journal of the Acoustical Society of America* 156(2):R3–R4. DOI 10.1121/10.0028150 —
  <https://pubmed.ncbi.nlm.nih.gov/39162417/>
- Heffner HE, Heffner RS, Contos C, Ott T (1994). "Audiogram of the hooded Norway rat". *Hearing
  Research* 73(2):244–247. DOI 10.1016/0378-5955(94)90240-2 — <https://pubmed.ncbi.nlm.nih.gov/8188553/>
- Lauer AM, Slee SJ, May BJ (2011). "Acoustic Basis of Directional Acuity in Laboratory Mice". *Journal
  of the Association for Research in Otolaryngology* 12(5):633–645. DOI 10.1007/s10162-011-0279-y —
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC3173556>
- Coles RB, Guppy A (1986). "Biophysical Aspects of Directional Hearing in the Tammar Wallaby, *Macropus
  eugenii*". *Journal of Experimental Biology* 121(1):371–394 —
  <https://journals.biologists.com/jeb/article/121/1/371/4930/Biophysical-Aspects-of-Directional-Hearing-in-the>
- Aitkin LM, Nelson JE, Shepherd RK (1994). "Hearing, vocalization and the external ear of a marsupial,
  the northern Quoll, *Dasyurus hallucatus*". *Journal of Comparative Neurology* 349(3):377–388. DOI
  10.1002/cne.903490305 — <https://pubmed.ncbi.nlm.nih.gov/7852631/>
- Cone-Wesson BK, Hill KG, Liu GB (1997). "Auditory brainstem response in tammar wallaby (*Macropus
  eugenii*)". *Hearing Research* 105(1–2):119–129. DOI 10.1016/s0378-5955(96)00199-2 —
  <https://pubmed.ncbi.nlm.nih.gov/9083809/>
- ter Hofstede HM, Goerlitz HR, Ratcliffe JM, Holderied MW, Surlykke A (2013). "The simple ears of
  noctuoid moths are tuned to the calls of their sympatric bat community". *Journal of Experimental
  Biology* 216(21):3954–3962. DOI 10.1242/jeb.093294 —
  <https://journals.biologists.com/jeb/article/216/21/3954/11663/>
- Römer H (2020). "Directional hearing in insects: biophysical, physiological and ecological challenges".
  *Journal of Experimental Biology* 223(14):jeb203224. DOI 10.1242/jeb.203224 —
  <https://journals.biologists.com/jeb/article/223/14/jeb203224/224498/>
- Miles RN, Robert D, Hoy RR (1995). "Mechanically coupled ears for directional hearing in the parasitoid
  fly *Ormia ochracea*". *Journal of the Acoustical Society of America* 98(6):3059–3070. DOI
  10.1121/1.413830 — <https://pubmed.ncbi.nlm.nih.gov/8550933/>
- Hoy RR (2006). "A boost for hearing in mosquitoes". *Proceedings of the National Academy of Sciences*
  103(45):16619–16620. DOI 10.1073/pnas.0608105103 — <https://pmc.ncbi.nlm.nih.gov/articles/PMC1636503/>
- Yorozu S, Wong A, Fischer BJ, Dankert H, Kernan MJ, Kamikouchi A, Ito K, Anderson DJ (2009). "Distinct
  sensory representations of wind and near-field sound in the *Drosophila* brain". *Nature*
  458(7235):201–205. DOI 10.1038/nature07843 — <https://pubmed.ncbi.nlm.nih.gov/19279637/>
- Schnitzler H-U, Kalko EKV (2001). "Echolocation by Insect-Eating Bats". *BioScience* 51(7):557–569. DOI
  10.1641/0006-3568(2001)051[0557:EBIEB]2.0.CO;2 —
  <https://academic.oup.com/bioscience/article-abstract/51/7/557/268230>
- Lawrence BD, Simmons JA (1982). "Measurements of atmospheric attenuation at ultrasonic frequencies and
  the significance for echolocation by bats". *Journal of the Acoustical Society of America*
  71(3):585–590. DOI 10.1121/1.387529

### Geometrical acoustics

- Savioja L, Svensson UP (2015). "Overview of geometrical room acoustic modeling techniques". *Journal of
  the Acoustical Society of America* 138(2):708–730. DOI 10.1121/1.4926438 —
  <https://pubmed.ncbi.nlm.nih.gov/26328688/>
- Savioja L, Xiang N (2020). "Simulation-Based Auralization of Room Acoustics". *Acoustics Today*
  16(4):48–55. DOI 10.1121/AT.2020.16.4.48 —
  <https://acousticstoday.org/wp-content/uploads/2020/12/Simulation-Based-Auralization-of-Room-Acoustics-Lauri-Savioja-and-Ning-Xiang.pdf>
- Krokstad A, Strøm S, Sørsdal S (1968). "Calculating the acoustical room response by the use of a ray
  tracing technique". *Journal of Sound and Vibration* 8(1):118–125. DOI 10.1016/0022-460X(68)90198-3
- Allen JB, Berkley DA (1979). "Image method for efficiently simulating small-room acoustics". *Journal
  of the Acoustical Society of America* 65(4):943–950. DOI 10.1121/1.382599
- Borish J (1984). "Extension of the image model to arbitrary polyhedra". *Journal of the Acoustical
  Society of America* 75(6):1827–1836. DOI 10.1121/1.390983
- Vorländer M (1989). "Simulation of the transient and steady-state sound propagation in rooms using a
  new combined ray-tracing/image-source algorithm". *Journal of the Acoustical Society of America*
  86(1):172–178. DOI 10.1121/1.398336
- Rindel JH (2000). "The Use of Computer Modeling in Room Acoustics". *Journal of Vibroengineering* 2000
  No. 3(4):219–224 (International Conference BALTIC-ACOUSTIC 2000) —
  <https://www.odeon.dk/pdf/Vilnius_2000-rindel.pdf>
- Masović D (2021). "Room Acoustics (lecture notes)". TU Berlin, Fachgebiet Technische Akustik.
  arXiv:2111.01900 [physics.class-ph] — <https://arxiv.org/abs/2111.01900>
- Sabine WC (1922). *Collected Papers on Acoustics*. Harvard University Press, Cambridge —
  <https://archive.org/details/collectedpaperso00sabi>
- Eyring CF (1930). "Reverberation Time in 'Dead' Rooms". *Journal of the Acoustical Society of America*
  1(2A):217–241. DOI 10.1121/1.1915175
- Schroeder MR (1962). "Frequency-Correlation Functions of Frequency Responses in Rooms". *Journal of the
  Acoustical Society of America* 34(12):1819–1823. DOI 10.1121/1.1909136
- Heinz R (1993). "Binaural room simulation based on an image source model with addition of statistical
  methods to include the diffuse sound scattering of walls and to predict the reverberant tail". *Applied
  Acoustics* 38(2–4):145–159. DOI 10.1016/0003-682X(93)90048-B
- Schröder D (2011). *Physically Based Real-Time Auralization of Interactive Virtual Environments*.
  Dissertation, RWTH Aachen University; Logos Verlag Berlin, Aachener Beiträge zur Technischen Akustik
  Band 11, ISBN 978-3-8325-3031-0. The RAVEN framework and the "diffuse rain" formulation are
  attributable to this work via the RAVEN project documentation at <https://virtualacoustics.org/RAVEN> —
  <https://publications.rwth-aachen.de/record/50580>
- Funkhouser T, Carlbom I, Elko G, Pingali G, Sondhi M, West J (1998). "A beam tracing approach to
  acoustic modeling for interactive virtual environments". *SIGGRAPH '98*, 21–32. DOI
  10.1145/280814.280818
- Lauterbach C, Chandak A, Manocha D (2007). "Interactive sound rendering in complex and dynamic scenes
  using frustum tracing". *IEEE Transactions on Visualization and Computer Graphics* 13(6):1672–1679. DOI
  10.1109/TVCG.2007.70567
- Chandak A, Lauterbach C, Taylor M, Ren Z, Manocha D (2008). "AD-Frustum: Adaptive Frustum Tracing for
  Interactive Sound Propagation". *IEEE Transactions on Visualization and Computer Graphics*
  14(6):1707–1722. DOI 10.1109/TVCG.2008.111
- Siltanen S, Lokki T, Kiminki S, Savioja L (2007). "The room acoustic rendering equation". *Journal of
  the Acoustical Society of America* 122(3):1624–1635. DOI 10.1121/1.2766781
- Vorländer M (2013). "Computer simulations in room acoustics: Concepts and uncertainties". *Journal of
  the Acoustical Society of America* 133(3):1203–1213. DOI 10.1121/1.4788978 —
  <https://pubmed.ncbi.nlm.nih.gov/23463991/>
- Vorländer M (2020). *Auralization: Fundamentals of Acoustics, Modelling, Simulation, Algorithms and
  Acoustic Virtual Reality*, 2nd edition. Springer International Publishing, Cham, RWTHedition. DOI
  10.1007/978-3-030-51202-6 — cited here only for the general point that millisecond-scale histogram
  binning substantially reduces the required ray count; the specific "40×" figure quoted elsewhere in
  this repository has not been verified against this edition.

### Diffraction

- Keller JB (1962). "Geometrical Theory of Diffraction". *Journal of the Optical Society of America*
  52(2):116–130 — <https://doi.org/10.1364/JOSA.52.000116>
- Kouyoumjian RG, Pathak PH (1974). "A uniform geometrical theory of diffraction for an edge in a
  perfectly conducting surface". *Proceedings of the IEEE* 62(11):1448–1461. DOI 10.1109/PROC.1974.9651
- Tsingos N, Funkhouser T, Ngan A, Carlbom I (2001). "Modeling acoustics in virtual environments using
  the uniform theory of diffraction". *SIGGRAPH 2001*, 545–552. DOI 10.1145/383259.383323 —
  <https://gfx.cs.princeton.edu/pubs/Tsingos_2001_MAI/index.php>
- Biot MA, Tolstoy I (1957). "Formulation of Wave Propagation in Infinite Media by Normal Coordinates
  with an Application to Diffraction". *Journal of the Acoustical Society of America* 29(3):381–391. DOI
  10.1121/1.1908899
- Medwin H (1981). "Shadowing by finite noise barriers". *Journal of the Acoustical Society of America*
  69(4):1060–1064. DOI 10.1121/1.385684
- Svensson UP, Fred RI, Vanderkooy J (1999). "An analytic secondary source model of edge diffraction
  impulse responses". *Journal of the Acoustical Society of America* 106(5):2331–2344. DOI
  10.1121/1.428071
- Maekawa Z (1968). "Noise reduction by screens". *Applied Acoustics* 1(3):157–173. DOI
  10.1016/0003-682X(68)90020-0
- Menounou P (2001). "A correction to Maekawa's curve for the insertion loss behind barriers". *Journal
  of the Acoustical Society of America* 110(4):1828–1838. DOI 10.1121/1.1398050 —
  <https://pubmed.ncbi.nlm.nih.gov/11681364/>
- Kirsch C, Ewert SD (2024). "Filter-based first- and higher-order diffraction modeling for geometrical
  acoustics". *Acta Acustica* 8, Article 73. DOI 10.1051/aacus/2024059
- Schissler C, Mehra R, Manocha D (2014). "High-order diffraction and diffuse reflections for interactive
  sound propagation in large environments". *ACM Transactions on Graphics* 33(4), Article 39 (SIGGRAPH
  2014). DOI 10.1145/2601097.2601216 — <http://gamma-web.iacs.umd.edu/HIGHDIFF/paper.pdf>
- Schissler C, Mückl G, Calamia P (2021). "Fast diffraction pathfinding for dynamic sound propagation".
  *ACM Transactions on Graphics* 40(4):138:1–138:13. DOI 10.1145/3450626.3459751

### Wave methods, and why they are rejected

- Raghuvanshi N, Narain R, Lin MC (2009). "Efficient and accurate sound propagation using adaptive
  rectangular decomposition". *IEEE Transactions on Visualization and Computer Graphics* 15(5):789–801.
  DOI 10.1109/TVCG.2009.27 — <https://pubmed.ncbi.nlm.nih.gov/19590105/>
- Raghuvanshi N, Snyder J (2014). "Parametric wave field coding for precomputed sound propagation". *ACM
  Transactions on Graphics* 33(4), Article 38 (SIGGRAPH 2014). DOI 10.1145/2601097.2601184. Bake times
  across the five demo scenes are 4–20 hours (Citadel 12, Deck 13, Sanctuary 15, Necropolis 20, Foliage
  4) —
  <https://www.microsoft.com/en-us/research/publication/parametric-wave-field-coding-precomputed-sound-propagation/>
- Raghuvanshi N, Snyder J (2018). "Parametric directional coding for precomputed sound propagation". *ACM
  Transactions on Graphics* 37(4). DOI 10.1145/3197517.3201339
- Mehra R, Raghuvanshi N, Antani L, Chandak A, Curtis S, Manocha D (2013). "Wave-based sound propagation
  in large open scenes using an equivalent source formulation". *ACM Transactions on Graphics* 32(2). DOI
  10.1145/2451236.2451245
- Aalto University, Department of Media Technology, Virtual Acoustics research group (undated, accessed
  31 July 2026). "Wave-based modeling" —
  <https://research.cs.aalto.fi/acoustics/virtual-acoustics/research/room-acoustics-modeling/70-wave-based-modeling.html>
- Microsoft Research (undated, accessed 31 July 2026). "Project Triton — Immersive sound propagation" —
  <https://www.microsoft.com/en-us/research/project/project-triton/>
- Microsoft (2022). "Project Acoustics (public repository)". GitHub, microsoft/ProjectAcoustics —
  <https://github.com/microsoft/ProjectAcoustics>

### Real-time systems, and the numbers they ship with

- Schissler C, Manocha D (2011). "GSound: Interactive Sound Propagation for Games". *AES 41st
  International Conference: Audio for Games*, London, UK, 2–4 February 2011, paper P2-6 —
  <https://www.aes.org/e-lib/browse.cfm?elib=15770>
- Schissler C, Manocha D (2017). "Interactive Sound Propagation and Rendering for Large Multi-Source
  Scenes". *ACM Transactions on Graphics* 36(1):2:1–2:12. DOI 10.1145/3072959.2943779 —
  <http://gamma-web.iacs.umd.edu/MULTISOURCE/paper.pdf>
- Taylor M, Chandak A, Mo Q, Lauterbach C, Schissler C, Manocha D (undated, circa 2010). "iSound:
  Interactive GPU-based Sound Auralization in Dynamic Scenes". Technical report, University of North
  Carolina at Chapel Hill — <http://gamma-web.iacs.umd.edu/Sound/iSound/isound-tech_report.pdf>
- Taylor M, Chandak A, Mo Q, Lauterbach C, Schissler C, Manocha D (2012). "Guided Multiview Ray Tracing
  for Fast Auralization". *IEEE Transactions on Visualization and Computer Graphics* 18(11):1797–1810.
  DOI 10.1109/TVCG.2012.27 — <https://pubmed.ncbi.nlm.nih.gov/22291154/>
- Schäfer P, Fatela J, Vorländer M (2024). "Interpolation of scheduled simulation results for real-time
  auralization of moving sources". *Acta Acustica* 8, Article 9. DOI 10.1051/aacus/2023070. The authors
  judged linear and cubic-spline interpolation indistinguishable in auralisation quality and recommended
  linear for its lower computational overhead —
  <https://acta-acustica.edpsciences.org/articles/aacus/full_html/2024/01/aacus230100/aacus230100.html>
- Valve Corporation (2026, version 4.8.1 at time of checking). "Steam Audio". GitHub,
  ValveSoftware/steam-audio; Apache License 2.0 — <https://github.com/ValveSoftware/steam-audio>
- Valve Corporation (2026). "SteamAudioSettings.cs (Unity integration runtime settings)". GitHub,
  ValveSoftware/steam-audio, master branch —
  <https://raw.githubusercontent.com/ValveSoftware/steam-audio/master/unity/src/project/SteamAudioUnity/Assets/Plugins/SteamAudio/Scripts/Runtime/SteamAudioSettings.cs>
- Valve Corporation (undated, accessed 31 July 2026). "Steam Audio C API documentation — Programmer's
  Guide" — <https://valvesoftware.github.io/steam-audio/doc/capi/guide.html>
- Valve Corporation (undated, accessed 31 July 2026). "Steam Audio C API documentation — Simulation" —
  <https://valvesoftware.github.io/steam-audio/doc/capi/simulation.html>
- Valve Corporation (n.d.). "Scene — Steam Audio C API documentation". Steam Audio documentation,
  copyright 2017–2023. Accessed 31 July 2026 —
  <https://valvesoftware.github.io/steam-audio/doc/capi/scene.html>
- Valve Corporation (undated, accessed 31 July 2026). "Steam Audio Material (Unity integration
  documentation)" — <https://valvesoftware.github.io/steam-audio/doc/unity/material.html>
- Alarcon N (NVIDIA) (22 March 2019). "VRWorks Audio Dials Up the Immersion with RTX Acceleration".
  NVIDIA Developer Blog —
  <https://developer.nvidia.com/blog/vrworks-audio-dials-up-the-immersion-with-rtx-acceleration/>
- Alary B (27 August 2020). "A Wwise Approach to Spatial Audio — Part 3 — Beyond Early Reflections".
  Audiokinetic Blog —
  <https://www.audiokinetic.com/en/blog/a-wwise-approach-to-spatial-audio-part-3-beyond-early-reflections/>
- Audiokinetic (author and date not established). "A Wwise Approach to Spatial Audio — Part 2 —
  Diffraction". Audiokinetic Blog —
  <https://www.audiokinetic.com/en/blog/a-wwise-approach-to-spatial-audio-part-2-diffraction/>
- Free Software Foundation (2026, list as retrieved). "Various Licenses and Comments about Them" —
  establishes that Apache-2.0 is compatible with GPL version 3, so Steam Audio may lawfully be read and
  reused here, though ARCHITECTURE.md's no-third-party-audio-library rule means it is reading material
  rather than a dependency — <https://www.gnu.org/licenses/license-list.en.html>
- The MathWorks, Inc. (2026, documentation as retrieved). "Room Impulse Response Simulation with
  Stochastic Ray Tracing (Audio Toolbox example)". Vendor documentation for a commercial product rather
  than a peer-reviewed source —
  <https://www.mathworks.com/help/audio/ug/room-impulse-response-simulation-with-stochastic-ray-tracing.html>
- LCAV, EPFL (Scheibler R, Bezzam E, Dokmanić I and contributors) (2026, repository as retrieved).
  "pyroomacoustics". GitHub, LCAV/pyroomacoustics; MIT licence —
  <https://github.com/LCAV/pyroomacoustics>
- Schissler C (author), Tang Z (maintainer) (2026, repository as retrieved). "pygsound — impulse response
  generation based on the GSound geometric sound propagation engine". GitHub, GAMMA-UMD/pygsound —
  <https://github.com/GAMMA-UMD/pygsound>

### Materials, media and standards

- International Organization for Standardization (1993). "ISO 9613-1:1993 — Acoustics — Attenuation of
  sound during propagation outdoors — Part 1: Calculation of the absorption of sound by the atmosphere" —
  <https://www.iso.org/standard/17426.html>
- International Organization for Standardization (1996). "ISO 9613-2:1996 — Acoustics — Attenuation of
  sound during propagation outdoors — Part 2: General method of calculation". Cited here **only** for
  Table 2, the octave-band atmospheric attenuation coefficients, and for the geometrical divergence and
  atmospheric absorption formulae. Superseded by ISO 9613-2:2024 —
  <https://www.iso.org/standard/20649.html>
- Bass HE, Sutherland LC, Zuckerwar AJ, Blackstock DT, Hester DM (1995). "Atmospheric absorption of
  sound: Further developments". *Journal of the Acoustical Society of America* 97(1):680–683 (erratum:
  *JASA* 99(2):1259, 1996). DOI 10.1121/1.412989
- International Organization for Standardization (2009). "ISO 3382-1:2009 — Acoustics — Measurement of
  room acoustic parameters — Part 1: Performance spaces" — <https://www.iso.org/standard/40979.html>
- International Organization for Standardization (2003). "ISO 354:2003 — Acoustics — Measurement of sound
  absorption in a reverberation room" — <https://www.iso.org/standard/34545.html>
- International Organization for Standardization (1997). "ISO 266:1997 — Acoustics — Preferred
  frequencies" — <https://www.iso.org/standard/1350.html>
- International Organization for Standardization (2004). "ISO 17497-1:2004 — Acoustics — Sound-scattering
  properties of surfaces — Part 1: Measurement of the random-incidence scattering coefficient in a
  reverberation room" — <https://www.iso.org/standard/31397.html>
- International Organization for Standardization (2012). "ISO 17497-2:2012 — Acoustics — Sound-scattering
  properties of surfaces — Part 2: Measurement of the directional diffusion coefficient in a free field"
  — <https://www.iso.org/standard/55293.html>
- International Organization for Standardization (2021). "ISO 10140-2:2021 — Acoustics — Laboratory
  measurement of sound insulation of building elements — Part 2: Measurement of airborne sound
  insulation" — <https://www.iso.org/standard/79487.html>
- Cox TJ, Dalenback B-I, D'Antonio P, Embrechts J-J, Jeon JY, Mommertz E, Vorländer M (2006). "A tutorial
  on scattering and diffusion coefficients for room acoustic surfaces". *Acta Acustica united with
  Acustica* 92(1):1–15 — <https://orbi.uliege.be/handle/2268/22992>
- Zeng X, Christensen CL, Rindel JH (2006). "Practical methods to define scattering coefficients in a
  room acoustics computer model". *Applied Acoustics* 67(8):771–786. DOI 10.1016/j.apacoust.2005.12.001 —
  <https://odeon.dk/pdf/A15-ApplAc_2006_Zeng_Lynge_Rindel.pdf>
- Cerdá S, Lacatis R, Giménez A (2013). "On absorption and scattering coefficient effects in
  modellisation software". *Acoustics Australia* 41(2):151–155. Cited here only for the
  energy-conservation identity — <https://acoustics.asn.au/journal/2013/2013_41_2_Cerda_paper.pdf>
- Treble Technologies (2026). "Absorption coefficients — Material input". Product documentation —
  <https://docs.treble.tech/material-input/absorption-coefficients>
- Irvine T (18 May 2012). "Acoustic Transmission Loss, Revision F". Vibrationdata technical tutorial —
  <https://www.vibrationdata.com/tutorials/aco_tl.pdf>
- LCAV, École Polytechnique Fédérale de Lausanne (2026). "pyroomacoustics materials database
  (materials.json)" —
  <https://raw.githubusercontent.com/LCAV/pyroomacoustics/master/pyroomacoustics/data/materials.json>
