/*
    Copyright (C) 2026 The AiQuokkaWannabe Authors https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify it under the terms of
    the GNU General Public License as published by the Free Software Foundation, either version
    3 of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
    without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along with this program.
    If not, see https://www.gnu.org/licenses/.
*/

#include "orbs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

namespace
{

    //! Twice subdivided: 320 faces, round enough that its reflection in the mirror floor reads as
    //! a ball and not a die, and a few kilobytes of hierarchy per glow level.
    constexpr std::uint32_t SUBDIVISIONS{2u};

    using Face = std::array<std::uint32_t, 3>;
    using EdgeMidpoints = std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t>;

    //! An edge's midpoint pushed out to the unit sphere, shared by both faces that meet there.
    [[nodiscard]] std::uint32_t midpoint(std::vector<MathLib::Vec3>& vertices, EdgeMidpoints& made, const std::uint32_t a, const std::uint32_t b)
    {
        const std::pair<std::uint32_t, std::uint32_t> key{std::min(a, b), std::max(a, b)};
        const auto known{made.find(key)};
        if (known != made.end()) {
            return known->second;
        }
        vertices.push_back(((vertices[a] + vertices[b]) * 0.5f).normalised());
        const std::uint32_t index{static_cast<std::uint32_t>(vertices.size() - 1u)};
        made.emplace(key, index);
        return index;
    }

} // namespace

namespace OrbLib
{

    std::uint32_t glowLevel(const float quantity) noexcept
    {
        const float clamped{std::clamp(quantity, 0.0f, 1.0f)};
        return std::clamp(static_cast<std::uint32_t>(std::ceil(clamped * static_cast<float>(GLOW_LEVELS))), 1u, GLOW_LEVELS);
    }

    Material material(const std::uint32_t level) noexcept
    {
        const float glow{FULL_EMISSION * static_cast<float>(std::clamp(level, 1u, GLOW_LEVELS)) / static_cast<float>(GLOW_LEVELS)};
        return makeEmissive(MathLib::Vec3{0.90f, 0.90f, 0.90f}, MathLib::Vec3{glow, glow, glow});
    }

    std::vector<BvhLib::Triangle> unitOrb(const std::uint32_t material_slot)
    {
        // The icosahedron: three golden rectangles, every vertex then pushed to the unit sphere.
        const float t{(1.0f + std::sqrt(5.0f)) * 0.5f};
        std::vector<MathLib::Vec3> vertices{MathLib::Vec3{-1.0f, t, 0.0f}, MathLib::Vec3{1.0f, t, 0.0f}, MathLib::Vec3{-1.0f, -t, 0.0f}, MathLib::Vec3{1.0f, -t, 0.0f},
            MathLib::Vec3{0.0f, -1.0f, t}, MathLib::Vec3{0.0f, 1.0f, t}, MathLib::Vec3{0.0f, -1.0f, -t}, MathLib::Vec3{0.0f, 1.0f, -t}, MathLib::Vec3{t, 0.0f, -1.0f},
            MathLib::Vec3{t, 0.0f, 1.0f}, MathLib::Vec3{-t, 0.0f, -1.0f}, MathLib::Vec3{-t, 0.0f, 1.0f}};
        for (MathLib::Vec3& vertex : vertices) {
            vertex = vertex.normalised();
        }
        std::vector<Face> faces{Face{0u, 11u, 5u}, Face{0u, 5u, 1u}, Face{0u, 1u, 7u}, Face{0u, 7u, 10u}, Face{0u, 10u, 11u}, Face{1u, 5u, 9u}, Face{5u, 11u, 4u},
            Face{11u, 10u, 2u}, Face{10u, 7u, 6u}, Face{7u, 1u, 8u}, Face{3u, 9u, 4u}, Face{3u, 4u, 2u}, Face{3u, 2u, 6u}, Face{3u, 6u, 8u}, Face{3u, 8u, 9u},
            Face{4u, 9u, 5u}, Face{2u, 4u, 11u}, Face{6u, 2u, 10u}, Face{8u, 6u, 7u}, Face{9u, 8u, 1u}};

        // Each face into four, the new vertices pushed out to the sphere.
        for (std::uint32_t pass{0u}; pass < SUBDIVISIONS; ++pass) {
            EdgeMidpoints made;
            std::vector<Face> finer;
            finer.reserve(faces.size() * 4u);
            for (const Face& face : faces) {
                const std::uint32_t ab{midpoint(vertices, made, face[0], face[1])};
                const std::uint32_t bc{midpoint(vertices, made, face[1], face[2])};
                const std::uint32_t ca{midpoint(vertices, made, face[2], face[0])};
                finer.push_back(Face{face[0], ab, ca});
                finer.push_back(Face{face[1], bc, ab});
                finer.push_back(Face{face[2], ca, bc});
                finer.push_back(Face{ab, bc, ca});
            }
            faces = std::move(finer);
        }

        std::vector<BvhLib::Triangle> triangles;
        triangles.reserve(faces.size());
        for (const Face& face : faces) {
            const MathLib::Vec3& v0{vertices[face[0]]};
            triangles.push_back(BvhLib::Triangle{
                .v0 = v0, .material = material_slot, .edge1 = vertices[face[1]] - v0, .padding0 = 0u, .edge2 = vertices[face[2]] - v0, .padding1 = 0u});
        }
        return triangles;
    }

    MathLib::Mat4 transform(const LnkFoodState& food) noexcept
    {
        return MathLib::Mat4::translate(MathLib::Vec3{food.position[0], food.position[1], food.position[2]})
            * MathLib::Mat4::scale(MathLib::Vec3{food.radius, food.radius, food.radius});
    }

    std::uint32_t appendShapes(std::vector<BvhLib::Bvh>& geometries, std::vector<Material>& materials)
    {
        const std::uint32_t first{static_cast<std::uint32_t>(geometries.size())};
        for (std::uint32_t level{1u}; level <= GLOW_LEVELS; ++level) {
            const std::uint32_t slot{static_cast<std::uint32_t>(materials.size())};
            materials.push_back(material(level));
            geometries.push_back(BvhLib::build(unitOrb(slot)));
        }
        return first;
    }

} // namespace OrbLib
