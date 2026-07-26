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

#include <math/matrix.hpp>
#include <math/quaternion.hpp>
#include <math/vector.hpp>
#include <cstddef>
#include <cstdint>

/*!
    Plain data components for the entity/component scene structure.

    These are stored in parallel Structure of Arrays (SoA) inside the Scene class. No inheritance,
    no virtual functions — pure data that maps directly to GPU storage buffers, which the compute
    shaders read while they walk the hand-built BVH.
*/

//! Spatial transform — position, orientation, and scale in world space.
struct Transform {
    MathLib::Vec3 position{0.0f, 0.0f, 0.0f}; //!< World-space position, in metres.
    MathLib::Quat orientation{MathLib::Quat::identity()}; //!< Orientation quaternion.
    MathLib::Vec3 scale{1.0f, 1.0f, 1.0f}; //!< Scale along each axis.

    //! Computes the model matrix (translation * rotation * scale).
    [[nodiscard]] MathLib::Mat4 modelMatrix() const
    {
        return MathLib::Mat4::translate(position) * orientation.toMat4() * MathLib::Mat4::scale(scale);
    }

    //! Computes the inverse model matrix, used to bring a world-space ray into object space.
    [[nodiscard]] MathLib::Mat4 inverseModelMatrix() const
    {
        return modelMatrix().inversed();
    }
};

//! Bounding sphere for coarse spatial queries and BVH construction.
struct Bounds {
    MathLib::Vec3 centre{0.0f, 0.0f, 0.0f}; //!< World-space bounding sphere centre, in metres.
    float radius{0.0f}; //!< Bounding sphere radius, in metres.
};

/*!
    Identifies the triangles belonging to this entity.

    Lite has no mesh registry and no meshlets. All triangles of the whole world live in one flat
    storage buffer, and the BVH the project builds itself indexes into that buffer. An entity
    therefore just owns a contiguous range of it.
*/
struct Geometry {
    uint32_t first_triangle{0u}; //!< Index of this entity's first triangle in the global triangle buffer.
    uint32_t triangle_count{0u}; //!< Number of triangles owned by this entity.
};

//! Identifies which material this entity uses (index into the scene's material table).
struct MaterialIndex {
    uint32_t index{0u}; //!< Material index.
};

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
    with no repacking. It is four 16-byte rows, 64 bytes in total, and every member sits at an
    offset that std430 would have picked anyway:

    | Offset | Size | Member                 | std430 equivalent |
    |-------:|-----:|------------------------|-------------------|
    |      0 |   12 | `colour`               | `float3`          |
    |     12 |    4 | `index_of_refraction`  | `float`           |
    |     16 |   12 | `emission`             | `float3`          |
    |     28 |    4 | `transmission`         | `float`           |
    |     32 |   16 | `acoustic_absorption`  | `float4`          |
    |     48 |    4 | `acoustic_scattering`  | `float`           |
    |     52 |   12 | `padding`              | explicit tail pad |

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

    /*!
        Acoustic absorption coefficient per frequency band, in the range 0 (perfectly reflective) to
        1 (perfectly absorbing). The four components are four octave bands, low to high.

        Reserved: the very same BVH is intended to carry acoustic rays later, so that a creature's
        ears can be traced against exactly the geometry its eyes see. Nothing reads this field yet,
        and the band boundaries are not fixed — treat the values as placeholders.
    */
    MathLib::Vec4 acoustic_absorption{0.0f, 0.0f, 0.0f, 0.0f};

    /*!
        Acoustic scattering coefficient, in the range 0 (purely specular reflection) to 1 (fully
        diffuse reflection).

        Reserved alongside acoustic_absorption; likewise unused for now.
    */
    float acoustic_scattering{0.0f};

    float padding[3]{0.0f, 0.0f, 0.0f}; //!< Explicit tail padding to a 16-byte boundary. Never read.
};

static_assert(sizeof(MathLib::Vec3) == 12u, "MathLib::Vec3 must be three tightly packed floats for the std430 material layout.");
static_assert(sizeof(MathLib::Vec4) == 16u, "MathLib::Vec4 must be four tightly packed floats for the std430 material layout.");
static_assert(sizeof(Material) == 64u, "Material must be exactly four 16-byte std430 rows.");
static_assert(alignof(Material) == 16u, "Material must be 16-byte aligned to match std430.");
static_assert(offsetof(Material, index_of_refraction) == 12u, "Material::index_of_refraction must sit in the first std430 row.");
static_assert(offsetof(Material, emission) == 16u, "Material::emission must start the second std430 row.");
static_assert(offsetof(Material, transmission) == 28u, "Material::transmission must sit in the second std430 row.");
static_assert(offsetof(Material, acoustic_absorption) == 32u, "Material::acoustic_absorption must start the third std430 row.");
static_assert(offsetof(Material, acoustic_scattering) == 48u, "Material::acoustic_scattering must start the fourth std430 row.");

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
