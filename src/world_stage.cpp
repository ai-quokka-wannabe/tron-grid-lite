/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify it under the terms of
    the GNU General Public License as published by the Free Software Foundation, either version
    3 of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
    without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along with this program.
    If not, see https://www.gnu.org/licenses/.
*/

#include "world_stage.hpp"

#include "stage.hpp"

#include <math/matrix.hpp>

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

    /*!
        The placeholder body: a neon dart the size the roster says a body is.

        Six vertices, eight faces. The nose reaches -Z because `forwardFor` faces -Z at rest, and
        the waist sits well behind the middle so the long slender end is unmistakably the front —
        a spectator's first question about a creature is which way it is going. Height comes from
        the roster's own constant so the placeholder stands exactly as tall as the body physics
        moves; length likewise. Width has no constant yet, so the dart is simply narrower than it
        is long, which is what makes the heading readable from above.
    */
    [[nodiscard]] std::vector<BvhLib::Triangle> makePlaceholderBody(const std::uint32_t material)
    {
        constexpr float LENGTH{RosterLib::BODY_HALF_LENGTH};
        constexpr float HEIGHT{RosterLib::BODY_HALF_HEIGHT};
        constexpr float WIDTH{0.08f};
        constexpr float WAIST_Z{LENGTH * 0.5f};

        const MathLib::Vec3 nose{0.0f, 0.0f, -LENGTH};
        const MathLib::Vec3 tail{0.0f, 0.0f, LENGTH};
        const MathLib::Vec3 right{WIDTH, 0.0f, WAIST_Z};
        const MathLib::Vec3 left{-WIDTH, 0.0f, WAIST_Z};
        const MathLib::Vec3 top{0.0f, HEIGHT, WAIST_Z};
        const MathLib::Vec3 bottom{0.0f, -HEIGHT, WAIST_Z};

        const std::array<std::array<MathLib::Vec3, 3>, 8> faces{{{nose, right, top}, {nose, top, left}, {nose, left, bottom}, {nose, bottom, right}, {tail, top, right},
            {tail, left, top}, {tail, bottom, left}, {tail, right, bottom}}};

        std::vector<BvhLib::Triangle> triangles;
        triangles.reserve(faces.size());
        for (const std::array<MathLib::Vec3, 3>& face : faces) {
            triangles.push_back(BvhLib::Triangle{
                .v0 = face[0], .material = material, .edge1 = face[1] - face[0], .padding0 = 0u, .edge2 = face[2] - face[0], .padding1 = 0u});
        }
        return triangles;
    }

}

namespace WorldStageLib
{

    WorldStage::WorldStage(BvhLib::Bvh grid, std::vector<Material> grid_materials, const std::uint32_t creature_capacity) :
        m_creature_capacity(creature_capacity),
        m_materials(std::move(grid_materials))
    {
        const std::uint32_t body_material{static_cast<std::uint32_t>(m_materials.size())};

        // Between the primary neon and the pillars in brightness: a being of light, not a lamp.
        m_materials.push_back(makeEmissive(MathLib::Vec3{0.60f, 0.85f, 0.95f}, MathLib::Vec3{2.00f, 3.40f, 4.00f}));

        m_scene.geometries.push_back(std::move(grid));
        m_scene.geometries.push_back(BvhLib::build(makePlaceholderBody(body_material)));
        m_scene.instances.push_back(BvhLib::makeInstance(m_scene.geometries.front(), 0u, MathLib::Mat4::identity()));

        std::uint32_t node_offset{0u};
        std::uint32_t triangle_offset{0u};
        m_node_offsets.reserve(m_scene.geometries.size());
        m_triangle_offsets.reserve(m_scene.geometries.size());
        for (const BvhLib::Bvh& geometry : m_scene.geometries) {
            m_node_offsets.push_back(node_offset);
            m_triangle_offsets.push_back(triangle_offset);
            node_offset += static_cast<std::uint32_t>(geometry.nodes.size());
            triangle_offset += static_cast<std::uint32_t>(geometry.triangles.size());
        }
    }

    BvhLib::FlatScene WorldStage::flatScene() const
    {
        return BvhLib::flatten(m_scene);
    }

    std::vector<BvhLib::InstanceRecord> WorldStage::records(const std::vector<WorldClientLib::InterpolatedCreature>& creatures) const
    {
        if (creatures.size() > m_creature_capacity) {
            throw std::runtime_error{
                "More creatures than this stage was built for: " + std::to_string(creatures.size()) + " of " + std::to_string(m_creature_capacity) + "."};
        }

        std::vector<BvhLib::InstanceRecord> result;
        result.reserve(1u + creatures.size());
        result.push_back(BvhLib::flattenInstance(m_scene.instances.front(), m_node_offsets[0], m_triangle_offsets[0],
            static_cast<std::uint32_t>(m_scene.geometries[0].nodes.size())));

        for (const WorldClientLib::InterpolatedCreature& creature : creatures) {
            const RosterLib::Pose pose{.position = MathLib::Vec3{creature.position[0], creature.position[1], creature.position[2]}, .yaw = creature.yaw};
            const BvhLib::Instance instance{BvhLib::makeInstance(m_scene.geometries[1], 1u, poseTransform(pose))};
            result.push_back(BvhLib::flattenInstance(instance, m_node_offsets[1], m_triangle_offsets[1], static_cast<std::uint32_t>(m_scene.geometries[1].nodes.size())));
        }
        return result;
    }

}
