# Materials

The material model, the Fresnel and refraction maths behind it, and the HDR path that carries
emissive neon from the shader to the screen.

## The Model, and Why It Is This Small

TronGrid Lite has three *named* materials — mirror, emissive and glass. They are conveniences
rather than categories: `Material` in `../src/components.hpp` is one continuous parameter space,
nothing downstream branches on which name produced a surface, and any blend of them is equally
valid. A glowing translucent mirror is as expressible as any of the three.

| Name | Behaviour | Parameters that define it |
|------|-----------|---------------------------|
| **Mirror** | Perfect specular reflection, Fresnel-weighted | `colour` (a tint on the reflection, usually close to white), `index_of_refraction`; zero `emission` and `transmission` |
| **Emissive** | Reflects, and additionally emits a constant radiance — the neon | `colour` plus a non-zero `emission` |
| **Glass** | Fresnel split between one reflected and one refracted ray | `colour` as tint, `index_of_refraction`, `transmission` towards 1 |

There is no roughness. No microfacet distribution, no diffuse lobe, no anisotropy, no textures.
Every surface in the world is either a perfect mirror, a light, or a clear pane, and that single
restriction is what makes the renderer both simple and fast.

The reason is worth stating plainly, because everything else in this document exists to support
it:

- **A perfectly specular surface has exactly one outgoing direction per incoming direction.**
  Reflection is `reflect(I, N)`; refraction is Snell's law. Neither involves a choice.
- **So each bounce is exactly one ray**, not a distribution of rays that must be sampled. Glass
  spawns two children (reflected and refracted), and the tree is capped at a shallow fixed depth,
  so the total ray count per pixel is a small constant known in advance.
- **A deterministic ray tree has no Monte Carlo variance.** There is nothing to average, so there
  is no noise. Two consecutive frames from the same camera pose are bit-identical on the same device;
  across GPUs they are close but not identical, which
  [PROGRAM_INTERFACE.md](PROGRAM_INTERFACE.md#determinism-is-per-device-and-this-is-measured-rather-than-assumed) measures.
- **Therefore there is no denoiser.** No temporal accumulation, no à-trous wavelet passes, no
  history buffers, no reprojection, no disocclusion handling, no warm-up phase — and none of the
  ghosting, lag or edge-leak artefacts that come with them.

This is Whitted's 1980 model, unchanged in its essentials, and it is a perfect fit for a world
made of neon and black glass. It also matters for perception: creature sensors render at very
small sample counts (see [PERCEPTION.md](PERCEPTION.md)), and a stochastic renderer at those
sample counts would deliver pure noise. A deterministic one delivers a clean image at any
resolution, however tiny.

The cost is that the model cannot represent a rough or matte surface at all. That is accepted.
The aesthetic is the algorithm.

---

## Fresnel

Fresnel reflectance governs both the mirrors and the glass, so it is the one piece of physics the
renderer cannot do without. It answers: at this viewing angle, what fraction of the incoming light
reflects rather than transmits?

### Schlick's Approximation

The exact Fresnel equations require complex arithmetic per wavelength. Schlick's 1994
approximation replaces them with a single power:

```hlsl
float3 fresnelSchlick(float cos_theta, float3 f0)
{
    float m = clamp(1.0 - cos_theta, 0.0, 1.0);
    float m2 = m * m;
    return f0 + (1.0 - f0) * (m2 * m2 * m);   // m^5, no pow() call
}
```

Average error is below 1 % for dielectrics in the IOR range 1.0–2.2, and it is roughly 32× cheaper
than the exact form. For a real-time renderer this is not a compromise worth agonising over.

Writing `m^5` as `m2 * m2 * m` avoids a transcendental `pow()` in the innermost loop of the trace.

### Deriving F0 from the Index of Refraction

`F0` is the reflectance at normal incidence — the value the curve starts from. For a dielectric
boundary between air and a medium of index `n`, it follows directly from the Fresnel equations at
`theta = 0`:

```text
F0 = ((n - 1) / (n + 1))^2
```

This should be computed from the material's IOR rather than authored as a separate number.
Storing both invites them to disagree, and a mirror whose `F0` does not match its `IOR` refracts
along one physical boundary while reflecting along another.

### What the Curve Does

At normal incidence a dielectric reflects very little — around 4 % for glass. At grazing angles
the Schlick term drives `F` towards 1.0 and every dielectric becomes a mirror. This single fact
produces most of the look of the world:

- A mirror surface reflects only a fraction of what hits it head-on and blazes with reflected neon
  at glancing angles. `colour` is a tint applied to the reflected light rather than an albedo, so
  a mirror stays close to white — the shipped floor is `(0.85, 0.90, 1.00)` — and its head-on
  dimness comes from `F0` alone.
- A glass panel is a clear window when faced directly and a mirror when seen edge-on.

### Indices of Refraction Worth Using

| Medium | IOR | Resulting F0 | Notes |
|--------|-----|--------------|-------|
| Vacuum / air | 1.000 | — | The medium rays travel through by default |
| Water | 1.333 | 0.020 | |
| Common glass | 1.500 | 0.040 | The default for the glass material |
| Volcanic glass (obsidian) | 1.486 | 0.038 | A real dark-glass reference, quoted for comparison only |
| Acrylic | 1.490 | 0.039 | |
| Sapphire | 1.770 | 0.077 | Noticeably brighter reflection |
| Diamond | 2.417 | 0.172 | Strong reflection, dramatic refraction |

Anything in the 1.45–1.55 band reads as "glass" and gives `F0 ≈ 0.04`, the familiar default
dielectric reflectance. Values above 2.0 look conspicuously artificial, which may well be what a
neon world wants.

The shipped mirror floor is one of those: it uses **2.4**, for `F0 ≈ 0.17`. The IOR is the only
reflectivity knob this material model has, and the honest 4 % of a real dark glass is far too dim
for a surface meant to read as a mirror. The reasoning is recorded beside the value, in
`makeMaterials()` in `../src/main.cpp`.

---

## Refraction

### Snell's Law

At a boundary between media of index `n_1` and `n_2`:

```text
n_1 * sin(theta_1) = n_2 * sin(theta_2)
```

The closed-form vector solution (what HLSL/Slang `refract(I, N, eta)` and the GLSL spec both
implement) is:

```text
k = 1 - eta^2 * (1 - (N . I)^2)
T = eta * I - (eta * (N . I) + sqrt(k)) * N     if k >= 0
  = float3(0, 0, 0)                             if k <  0
```

| Symbol | Meaning |
|--------|---------|
| `I` | Incident direction, normalised, pointing towards the surface |
| `N` | Surface normal, flipped if necessary to face the incident ray |
| `eta` | Ratio of indices `n_from / n_to` |
| `k` | Discriminant — negative means total internal reflection |
| `T` | Refracted direction |

Entering glass from air at IOR 1.5 gives `eta = 1 / 1.5 ≈ 0.667`. Leaving glass for air gives
`eta = 1.5`.

### Total Internal Reflection

When `k < 0` there is no transmitted direction: the ray is entirely reflected back into the denser
medium. This happens only on the dense-to-thin side of a boundary, above the critical angle:

```text
theta_critical = asin(n_to / n_from)
```

For glass to air at IOR 1.5 that is `asin(1 / 1.5) ≈ 41.8°`. Past that angle a glass slab becomes
a perfect internal mirror, which is why the edges of a glass panel look bright and metallic.

The shader convention is that `refract()` returns the zero vector on TIR, so the trace should test
the result and fall back to reflection:

```hlsl
float3 t = refract(incident, normal, eta);

if (dot(t, t) < 1.0e-8)
{
    t = reflect(incident, normal);   // Total internal reflection.
}
```

Comparing the squared length against an epsilon is safer than testing for exact equality with
zero, and cheaper than a `length()` call.

### Fresnel Split at a Glass Surface

A glass hit spawns two rays and blends them by the same Schlick term used for mirrors:

```text
F     = F0 + (1 - F0) * (1 - cos(theta_v))^5
final = (F * reflected) + ((1 - F) * transmission * refracted * colour) + emission
```

Fresnel decides the reflected share first; `transmission` then decides how much of the remainder
passes through rather than being absorbed, which is exactly the split `Material::transmission`
encodes. At `transmission = 0` the second term vanishes and the surface is the opaque mirror.

Multiplying the transmitted contribution by the material's tint colour is a Beer-Lambert-lite
approximation. Full Beer-Lambert attenuates by `exp(-sigma * d)` over the path length `d` inside
the medium; for a thin panel the linear tint is visually indistinguishable and avoids having to
measure the exit distance before shading.

### Why a Traced Refraction Beats a Screen-Space One

A screen-space refraction perturbs the texture coordinates of an already-rendered colour buffer.
It is cheap, and it is wrong in ways that are hard to hide:

- **It can only show what is already on screen.** Anything the refracted ray should reveal from
  outside the frustum, or hidden behind the glass from the camera's point of view, simply is not
  in the buffer. The usual workaround is to clamp to the screen edge, which smears.
- **It ignores the actual geometry of the boundary.** The offset is a screen-space fudge scaled by
  the normal, not a direction computed from Snell's law, so the bend does not respond correctly to
  IOR or to viewing angle.
- **It cannot do total internal reflection at all**, because there is no ray to fail to transmit.
- **It cannot handle stacked glass.** A second surface behind the first is not in the source
  buffer in a refracted state.

Because this renderer traces rays anyway, the honest version costs almost nothing extra: fire one
ray along `T` against the same BVH the primary rays use, and shade whatever it hits. TIR falls out
of the maths for free, glass behind glass works, and off-screen geometry is visible through the
pane exactly as it should be.

One practical detail: a refraction ray fired from the front face of a closed glass object will
immediately hit that object's own back face unless the traversal skips the originating primitive.
Carry the source primitive index down the ray and reject it during traversal, or offset the ray
origin off the surface by a small epsilon. The self-hit is the single most common bug in a
hand-written refraction path. This renderer takes the second option and nothing else —
`SURFACE_EPSILON` in `../src/trace.slang`.

---

## HDR Pipeline

### Why 16-Bit Float

Neon is the only light source in this world, and neon is bright. An emissive tube is authored at
an intensity well above 1.0 — the shipped tubes peak at 4.2 and 4.4 — because that is what makes
it read as a light rather than as a light-coloured surface.

An 8-bit UNORM colour target clamps everything above 1.0 on write. Doing so would:

- Flatten every emissive surface to the same uniform "on" colour, destroying the distinction
  between a dim tube and a searing one.
- Break bloom, which works by extracting pixels above a brightness threshold. If nothing exceeds
  1.0, there is nothing to extract and the glow disappears.
- Clip Fresnel highlights on mirrors at grazing angles, where reflected neon legitimately exceeds
  1.0.

So the renderer draws into `VK_FORMAT_R16G16B16A16_SFLOAT`: half-float per channel, 64 bits per
pixel. Half-float tops out at 65504 and holds a little over three significant decimal digits — far
more headroom than a scene whose brightest emissive sits at 5.0 will ever need, and enough
precision that the quantisation is invisible after tonemapping. It is also natively filtered and
blended at full rate on essentially every GPU that supports Vulkan 1.3. Full 32-bit float would
double the bandwidth for range and precision nobody can see.

The swapchain image stays 8-bit. Only the intermediate target is HDR; tonemapping bridges the two
at the very end.

### Emissive Intensity Units

`Material::emission` is a single linear `float3` holding radiance directly, so the intensity lives
in its magnitude. There is no separate scalar field. The authoring convention is nevertheless to
think of it as `palette_colour * intensity` and to write the product, keeping the two apart in the
call site rather than in the struct:

- The palette entry stays a normalised hue that can be compared across materials and tuned in one
  place, while intensity varies per instance.
- Because `emission` is float rather than UNORM, the product never clips, so a saturated hue
  survives being scaled. The shipped accent orange emits `(4.40, 1.60, 0.15)`: the low blue channel
  is preserved rather than being crushed to zero, and the red is not pinned at 1.0, either of which
  would shift the hue — towards white in the first case, towards red in the second.

Working magnitudes, against the shipped `EXPOSURE` of 1.0 and `BLOOM_THRESHOLD` of 1.0: below 1.0
for something that should not bloom at all, 4.0–5.0 for a neon tube that should bloom strongly.
The brightest emissive channel anywhere in the world is the pillar's 5.0, so anything much above
that is a deliberate departure from the scene's tone curve rather than a working value.

### Order of Operations

```text
1. Trace the ray tree in compute, writing linear HDR radiance into the R16G16B16A16_SFLOAT image
2. Bloom: threshold extraction, mip-chain downsample, tent-filter upsample
3. Post-process compute: composite bloom, apply the ACES fitted curve, encode sRGB,
   write an 8-bit UNORM image of its own
4. Blit that image into the acquired swapchain image
```

The post-process pass does not touch the swapchain. It writes into its own `R8G8B8A8_UNORM` storage
image and leaves it in `eTransferSrcOptimal`; the frame loop then blits that image across. The blit
is what converts the traced RGBA channel order into the surface's BGRA, which is also why it is a
blit and not a plain copy.

Writing into a UNORM storage image rather than an `_SRGB` one means the write performs no
automatic sRGB encoding, so the shader must do the encoding itself, which it does exactly (see
below).

Nothing in this list is stochastic, so nothing in it needs a history buffer.

---

## Tonemapping

Tonemapping maps unbounded linear HDR radiance into the `[0, 1]` display range without simply
clipping it.

### The ACES Fitted Curve

The renderer uses the **ACES fitted RRT+ODT with AP1 hue preservation** (the Hill / MJP BakingLab
formulation), not the cheaper Narkowicz analytic fit:

```text
linear HDR RGB (Rec.709)
  -> transform into ACES AP1
  -> per-channel RRT + ODT curve
  -> transform back to Rec.709 with the AP1 hue-preservation correction
  -> clamp to [0, 1]
  -> exact sRGB encoding
```

The extra matrix work buys hue stability on exactly the colours this world is made of. The
Narkowicz fit pushes bright saturated tones towards white, which turns orange neon yellow as it
brightens — the most conspicuous colour in the palette, ruined precisely where it is brightest.
The fitted curve holds the hue.

Alternatives considered and rejected:

- **Narkowicz ACES (2016)** — analytic and fast, but hue-shifts saturated emissives.
- **Reinhard** — `c / (1 + c)`. No toe, no hue preservation, washed-out highlights. Useful only as
  a reference point.

### Exact sRGB Encoding

Because the post-process compute shader writes to a UNORM storage image, it must apply the IEC
61966-2-1 transfer function itself. Use the piecewise definition, not the `pow(c, 1/2.2)`
shorthand:

```hlsl
float linearToSrgb(float c)
{
    return (c <= 0.0031308) ? (c * 12.92) : ((1.055 * pow(c, 1.0 / 2.4)) - 0.055);
}
```

The linear segment near zero exists because a pure power curve has infinite slope at the origin,
which quantises badly in the darks. In a world that is mostly black with a few bright lines, the
darks are most of the frame, and the difference between the exact curve and the 2.2 gamma
approximation is plainly visible as banding in the near-black mirror floor.

Clamp to `[0, 1]` before encoding — `pow()` on a negative input returns NaN.

---

## The Neon Palette

The look uses two neon colours: cyan as the primary grid colour, orange as an accent on major grid
lines.

| Colour | Linear RGB | Role |
|--------|-----------|------|
| Cyan | (0.02, 0.62, 1.0) | Primary — most grid lines |
| Orange | (1.0, 0.36, 0.03) | Accent — every 8th row and column |

Two colours rather than one gives the grid a readable sense of scale: the coarse orange supergrid
tells you how far you are looking across the fine cyan one. Two rather than three or more keeps
the palette coherent; a third hue starts to look like a colour test chart instead of a world.

The selection happens on the host, when the grid mesh is generated, and not in the shader at all.
Every edge whose row or column index is a multiple of `NeonTubeConfig::major_interval` is emitted
into the accent sub-mesh rather than the primary one, and the two sub-meshes are tagged with
`MATERIAL_NEON_ACCENT` and `MATERIAL_NEON_PRIMARY` as their triangles are appended to the
hierarchy. See `isMajorGridLine()` in `../src/geometry.cpp`.

An edge is an accent edge when *either* of its axes lies on a major line, so that the crossings of
a major row and a major column stay a single unbroken accent lattice instead of breaking at every
junction. With the shipped `major_interval` of 8 that puts roughly 23 % of the edges on the accent
colour.

Deciding this once on the host is what keeps reflections correct, at no cost in the shader: the
material index rides on the triangle, so a reflected or refracted hit reads exactly the index a
primary hit would, with no function that has to be kept consistent in two places.

---

## Numerical Stability

A deterministic renderer removes noise, but not the ways floating-point arithmetic can produce a
black pixel, a white pixel, or a NaN that poisons everything downstream.

| Guard | Value | Purpose |
|-------|-------|---------|
| Fresnel clamp | `clamp(1 - cos_theta, 0, 1)` | Prevents `pow()` on a negative base |
| Throughput cutoff | `1 / 512` | Terminates a ray whose accumulated attenuation can no longer change the pixel |
| TIR test | `dot(T, T) < 1e-8` | Detects the zero vector from `refract()` without an exact compare |
| Ray origin offset | `1e-3` along the normal, on whichever side the new ray leaves | Prevents self-intersection (shadow / reflection acne) |
| Minimum ray distance | `1e-8` | Rejects the degenerate zero-distance hit during traversal; the surface offset above is what actually cures acne |
| Inverse ray direction | `1e-30` in place of a zero component | An axis-aligned ray gives `1 / 0` in the BVH slab test, and an origin lying on that slab's plane then computes `0 * inf` = NaN, which turns a box the ray does pass through into a miss. The shader and the host BVH substitute the same tiny magnitude |
| Pre-encode clamp | `clamp(colour, 0, 1)` | Guarantees `linearToSrgb()` never sees a negative input |
| Normalise after interpolation | Always | Interpolated normals are not unit length; an unnormalised `N` silently corrupts both `reflect()` and `refract()` |

Two further notes:

- **Flip the normal to face the incident ray before using it.** Both `reflect()` and `refract()`
  assume `N` opposes `I`. On a back-facing hit — which every refraction ray exiting a solid
  produces — the stored normal points the wrong way, and the `eta` ratio must be inverted at the
  same time. Getting one right and the other wrong yields glass that looks plausible from outside
  and inverted from within.
- **Cap the ray tree depth explicitly.** A glass hit spawns two children, so an uncapped tree
  grows as `2^depth`. A depth of 4 to 5 is ample for this material set; beyond that the
  contributions are below the display's quantisation step anyway. Terminate on a throughput
  threshold as well as on depth, so a ray that has already been attenuated to near-nothing stops
  early.

Because there is no accumulation buffer, a NaN that does appear affects exactly one pixel of one
frame rather than persisting in a history buffer forever. That is a small but real robustness
benefit of the deterministic design.

---

## References

- Whitted, T. (1980). *An improved illumination model for shaded display*. CACM 23(6), 343–349.
- Schlick, C. (1994). *An inexpensive BRDF model for physically-based rendering*. Computer Graphics
  Forum 13(3), 233–246.
- [Hill / MJP — ACES fitted RRT+ODT (BakingLab)](https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl)
- IEC 61966-2-1:1999. *Multimedia systems and equipment — Colour measurement and management —
  Part 2-1: Default RGB colour space — sRGB*.
