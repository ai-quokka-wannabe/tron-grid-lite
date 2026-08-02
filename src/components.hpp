/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#pragma once

#include <math/vector.hpp>
#include <cstddef>
#include <cstdint>

/*!
    The surface description the compute shaders read.

    One struct, because one is all the renderer uses. This header once carried an
    entity/component scene layout — Transform, Bounds, Geometry, MaterialIndex, stored as parallel
    arrays inside a Scene class — and none of it was ever instantiated: the Grid is built
    directly in main.cpp as triangle soup and handed to the hierarchy builder. It has been removed
    rather than left as a promise, along with scene.hpp itself.
*/

/*!
    Surface description as the compute shaders see it.

    Every surface in Lite is a **perfectly smooth** one that is simultaneously reflective,
    translucent and emissive, in whatever proportions its parameters say. There are no material
    *types* and no branch in the shader: "mirror", "neon" and "glass" are simply regions of one
    continuous parameter space, and a glowing translucent mirror is as expressible as any of them.

    This is not a departure from physically-based rendering — it is physically-based rendering at
    the smooth limit. As roughness tends to zero the microfacet distribution of a Cook-Torrance
    BRDF collapses to a delta function, and the entire model reduces analytically to Fresnel-
    weighted mirror reflection plus Snell refraction. That closed form is what this struct holds.
    The consequence is that the ray tree is deterministic and shallow (Whitted 1980 style): every
    intersection spawns at most one reflected and one refracted ray, so there is no Monte Carlo
    integration, no variance, no noise and therefore no denoiser anywhere in the renderer.

    What is deliberately absent: roughness, microfacet distributions, diffuse lobes and textures.

    The layout is chosen so that this struct can be memcpy'd straight into an std430 storage buffer
    with no repacking. It is two 16-byte rows, 32 bytes in total, with no padding anywhere — every
    member sits at the offset std430 would have picked anyway:

    | Offset | Size | Member                 | std430 equivalent |
    |-------:|-----:|------------------------|-------------------|
    |      0 |   12 | `colour`               | `float3`          |
    |     12 |    4 | `index_of_refraction`  | `float`           |
    |     16 |   12 | `emission`             | `float3`          |
    |     28 |    4 | `transmission`         | `float`           |

    Acoustic properties are deliberately **not** here, and that is now a decision rather than a
    deferral. They live in their own parallel table in `acoustics.hpp`, indexed by the same
    `MaterialSlot`, because the two senses disagree about what a surface is: an optically emissive
    pillar is acoustically silent, and keying a gather on `emission` would find 16,724 triangles
    where the design calls for 16,640. Two tables that disagree are the point, not an accident.

    No `bool` members appear anywhere, because the size and alignment of a GLSL/Slang `bool` in a
    storage buffer is not something worth betting on; anything switch-like must be a `uint32_t`.
*/
struct alignas(16) Material {
    MathLib::Vec3 colour{0.0f, 0.0f, 0.0f}; //!< Linear tint applied to reflected and transmitted light. Mirrors are typically almost black.
    float index_of_refraction{1.5f}; //!< Refractive index. Drives Fresnel for every surface, and Snell refraction when transmission is non-zero.

    MathLib::Vec3 emission{0.0f, 0.0f, 0.0f}; //!< Emitted radiance in watts per steradian per square metre. Non-zero makes the surface a light — the neon.

    /*!
        Fraction of non-reflected light that passes through the surface rather than being absorbed,
        from 0 (opaque) to 1 (fully translucent). Fresnel decides the reflected share first; this
        decides what happens to the remainder.
    */
    float transmission{0.0f};
};

static_assert(sizeof(MathLib::Vec3) == 12u, "MathLib::Vec3 must be three tightly packed floats for the std430 material layout.");
static_assert(sizeof(Material) == 32u, "Material must be exactly two 16-byte std430 rows.");
static_assert(alignof(Material) == 16u, "Material must be 16-byte aligned to match std430.");
static_assert(offsetof(Material, index_of_refraction) == 12u, "Material::index_of_refraction must sit in the first std430 row.");
static_assert(offsetof(Material, emission) == 16u, "Material::emission must start the second std430 row.");
static_assert(offsetof(Material, transmission) == 28u, "Material::transmission must sit in the second std430 row.");

/*!
    Slots in the Grid's material table.

    Both material tables are indexed by these — the optical one a triangle's `material` field selects
    in `trace.slang`, and the acoustic one in `acoustics.hpp`. The two are separate tables of the same
    length in the same order, so a slot names a *surface of the Grid* rather than a set of optical
    properties, and a surface may perfectly well be bright and silent.
*/
enum MaterialSlot : uint32_t {
    MATERIAL_FLOOR = 0u, //!< The mirror the whole Grid stands on.
    MATERIAL_NEON_PRIMARY = 1u, //!< Cyan tubes along ordinary grid lines.
    MATERIAL_NEON_ACCENT = 2u, //!< Orange tubes along major grid lines.
    MATERIAL_PILLAR = 3u, //!< Standing blocks, bright enough to light the floor around them.
    MATERIAL_GLASS = 4u, //!< Clear slabs that refract what is behind them.
    MATERIAL_GLOWING_GLASS = 5u, //!< A tube that emits and transmits at once.
    MATERIAL_SLOT_COUNT = 6u //!< Number of slots, and therefore the length of both material tables.
};

/*
    Named points in the material space. These are conveniences, not categories: nothing downstream
    branches on which one produced a material, and any blend of them is equally valid.
*/

//! An opaque perfect mirror of the given colour.
[[nodiscard]] constexpr Material makeMirror(const MathLib::Vec3& colour)
{
    Material material{};
    material.colour = colour;
    return material;
}

//! A mirror that also emits the given radiance — the neon.
[[nodiscard]] constexpr Material makeEmissive(const MathLib::Vec3& colour, const MathLib::Vec3& emission)
{
    Material material{};
    material.colour = colour;
    material.emission = emission;
    return material;
}

//! A fully translucent surface of the given tint, refracting by Snell's law.
[[nodiscard]] constexpr Material makeGlass(const MathLib::Vec3& colour, float index_of_refraction)
{
    Material material{};
    material.colour = colour;
    material.index_of_refraction = index_of_refraction;
    material.transmission = 1.0f;
    return material;
}

/*!
    A glowing translucent surface — the case the old three-type material model could not express,
    and precisely the one a neon tube with a glass envelope wants.
*/
[[nodiscard]] constexpr Material makeGlowingGlass(const MathLib::Vec3& colour, const MathLib::Vec3& emission, float index_of_refraction, float transmission)
{
    Material material{};
    material.colour = colour;
    material.emission = emission;
    material.index_of_refraction = index_of_refraction;
    material.transmission = transmission;
    return material;
}
