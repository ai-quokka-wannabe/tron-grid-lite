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
    The three — and only three — kinds of surface Lite supports.

    Keeping the set this small is what makes the ray tree deterministic and shallow (Whitted 1980
    style): every intersection spawns at most one reflected and one refracted ray, so there is no
    Monte Carlo integration, no noise, and consequently no denoiser. There is no roughness, no
    microfacet model and no textures.
*/
enum class MaterialKind : uint32_t {
    Mirror = 0u, //!< Perfect mirror of a single colour, usually close to black.
    Emissive = 1u, //!< Perfect mirror that also emits light — this is the neon.
    Glass = 2u //!< Transparent surface refracting by Snell's law with the given index of refraction.
};

/*!
    Surface description as the compute shaders see it.

    The layout is chosen so that this struct can be memcpy'd straight into an std430 storage buffer
    with no repacking. It is four 16-byte rows, 64 bytes in total, and every member sits at an
    offset that std430 would have picked anyway:

    | Offset | Size | Member                 | std430 equivalent |
    |-------:|-----:|------------------------|-------------------|
    |      0 |   12 | `colour`               | `float3`          |
    |     12 |    4 | `index_of_refraction`  | `float`           |
    |     16 |   12 | `emission`             | `float3`          |
    |     28 |    4 | `kind`                 | `uint`            |
    |     32 |   16 | `acoustic_absorption`  | `float4`          |
    |     48 |    4 | `acoustic_scattering`  | `float`           |
    |     52 |   12 | `padding`              | explicit tail pad |

    No `bool` members appear anywhere: the surface kind is an `enum class : uint32_t` and any future
    switches must be `uint32_t` flags, because the size and alignment of a GLSL/Slang `bool` in a
    storage buffer is not something worth betting on.
*/
struct alignas(16) Material {
    MathLib::Vec3 colour{0.0f, 0.0f, 0.0f}; //!< Linear surface colour. Mirrors are typically almost black.
    float index_of_refraction{1.5f}; //!< Refractive index, used by MaterialKind::Glass only. 1.5 is ordinary glass.

    MathLib::Vec3 emission{0.0f, 0.0f, 0.0f}; //!< Emitted radiance, in watts per steradian per square metre. Non-zero for MaterialKind::Emissive.
    MaterialKind kind{MaterialKind::Mirror}; //!< Which of the three surface kinds this is.

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
static_assert(offsetof(Material, kind) == 28u, "Material::kind must sit in the second std430 row.");
static_assert(offsetof(Material, acoustic_absorption) == 32u, "Material::acoustic_absorption must start the third std430 row.");
static_assert(offsetof(Material, acoustic_scattering) == 48u, "Material::acoustic_scattering must start the fourth std430 row.");

//! Returns a perfect mirror material of the given colour.
[[nodiscard]] constexpr Material makeMirror(const MathLib::Vec3& colour)
{
    Material material{};
    material.colour = colour;
    material.kind = MaterialKind::Mirror;
    return material;
}

//! Returns an emissive material — a mirror of the given colour that also emits the given radiance.
[[nodiscard]] constexpr Material makeEmissive(const MathLib::Vec3& colour, const MathLib::Vec3& emission)
{
    Material material{};
    material.colour = colour;
    material.emission = emission;
    material.kind = MaterialKind::Emissive;
    return material;
}

//! Returns a glass material of the given tint and index of refraction.
[[nodiscard]] constexpr Material makeGlass(const MathLib::Vec3& colour, float index_of_refraction)
{
    Material material{};
    material.colour = colour;
    material.index_of_refraction = index_of_refraction;
    material.kind = MaterialKind::Glass;
    return material;
}
