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

#include "stage.hpp"

#include "acoustics.hpp"

namespace
{

    //! A body material in the Grid's own table row: the same four quantities, field for field.
    [[nodiscard]] Material materialFrom(const TglRenderMaterial& offered)
    {
        Material material{};
        material.colour = MathLib::Vec3{offered.colour[0], offered.colour[1], offered.colour[2]};
        material.index_of_refraction = offered.index_of_refraction;
        material.emission = MathLib::Vec3{offered.emission[0], offered.emission[1], offered.emission[2]};
        material.transmission = offered.transmission;
        return material;
    }

    /*!
        A body's triangles in the world's form, material indices rewritten to their global slots.

        The rewrite happens here and nowhere else, which is the point of doing it at assembly: the
        model keeps its own local indices — what was accepted stays exactly what was offered — and
        the world sees only global ones, so neither side ever holds the other's numbering.
    */
    [[nodiscard]] std::vector<BvhLib::Triangle> trianglesFrom(const RosterLib::CreatureModel& model, uint32_t material_base)
    {
        std::vector<BvhLib::Triangle> triangles;
        triangles.reserve(model.triangles.size());

        for (const TglRenderTriangle& triangle : model.triangles) {
            const MathLib::Vec3& v0{model.vertex_positions[triangle.vertices[0]]};
            const MathLib::Vec3& v1{model.vertex_positions[triangle.vertices[1]]};
            const MathLib::Vec3& v2{model.vertex_positions[triangle.vertices[2]]};

            triangles.push_back(BvhLib::Triangle{
                .v0 = v0, .material = material_base + triangle.material, .edge1 = v1 - v0, .padding0 = 0u, .edge2 = v2 - v0, .padding1 = 0u});
        }

        return triangles;
    }

} // namespace

MathLib::Mat4 poseTransform(const RosterLib::Pose& pose)
{
    // Yaw first, translation second, which is what worldFromBody does one point at a time: a body
    // point is rotated about the body's own origin and then carried to where the body stands.
    return MathLib::Mat4::translate(pose.position) * MathLib::Mat4::rotate(MathLib::Vec3{0.0f, 1.0f, 0.0f}, pose.yaw);
}

Stage::Stage(BvhLib::Bvh grid, std::vector<Material> grid_materials, const std::vector<RosterLib::Creature>& creatures) :
    m_materials(std::move(grid_materials))
{
    /*
        One strength per combined slot, zero unless the Grid's authored table says otherwise —
        written zeros rather than a table fallen off the end of, so a body slot is silent by
        record. Sized from the optical table rather than from the authored one, because the
        optical table is what the slots index and a test may stage a smaller Grid than the real
        six materials.
    */
    m_acoustic_strengths.assign(m_materials.size(), 0.0f);
    const std::vector<float> authored{Acoustics::makeAcousticSourceStrengths()};
    for (size_t slot{0u}; (slot < authored.size()) && (slot < m_acoustic_strengths.size()); ++slot) {
        m_acoustic_strengths[slot] = authored[slot];
    }

    m_scene.geometries.push_back(std::move(grid));
    m_scene.instances.push_back(BvhLib::makeInstance(m_scene.geometries.front(), 0u, MathLib::Mat4::identity()));

    for (const RosterLib::Creature& creature : creatures) {
        if (creature.model.empty()) {
            continue; // A bodiless creature stands nowhere and costs nothing.
        }

        const uint32_t material_base{static_cast<uint32_t>(m_materials.size())};
        for (const TglRenderMaterial& offered : creature.model.materials) {
            m_materials.push_back(materialFrom(offered));
            m_acoustic_strengths.push_back(0.0f);
        }

        const uint32_t geometry_index{static_cast<uint32_t>(m_scene.geometries.size())};
        m_scene.geometries.push_back(BvhLib::build(trianglesFrom(creature.model, material_base)));

        Body body{};
        body.creature_id = creature.body.creature_id;
        body.instance = static_cast<uint32_t>(m_scene.instances.size());
        body.geometry = geometry_index;
        m_bodies.push_back(body);

        m_scene.instances.push_back(BvhLib::makeInstance(m_scene.geometries.back(), geometry_index, poseTransform(creature.pose)));
    }

    // The offsets every per-tick record will need, cached now because geometry never moves in the
    // concatenated buffers once uploaded.
    uint32_t node_offset{0u};
    uint32_t triangle_offset{0u};
    m_node_offsets.reserve(m_scene.geometries.size());
    m_triangle_offsets.reserve(m_scene.geometries.size());
    for (const BvhLib::Bvh& geometry : m_scene.geometries) {
        m_node_offsets.push_back(node_offset);
        m_triangle_offsets.push_back(triangle_offset);
        node_offset += static_cast<uint32_t>(geometry.nodes.size());
        triangle_offset += static_cast<uint32_t>(geometry.triangles.size());
    }
}

void Stage::update(const std::vector<RosterLib::Creature>& creatures)
{
    /*
        By identity rather than by position in the roster, because the roster may hold bodiless
        creatures between modelled ones and this class promised not to care. Linear search for the
        same reason the senses source uses one: a roster is a handful of creatures.
    */
    for (const Body& body : m_bodies) {
        for (const RosterLib::Creature& creature : creatures) {
            if (creature.body.creature_id == body.creature_id) {
                m_scene.instances[body.instance] = BvhLib::makeInstance(m_scene.geometries[body.geometry], body.geometry, poseTransform(creature.pose));
                break;
            }
        }
    }
}

uint32_t Stage::instanceOf(uint64_t creature_id) const noexcept
{
    for (const Body& body : m_bodies) {
        if (body.creature_id == creature_id) {
            return body.instance;
        }
    }
    return BvhLib::NO_INSTANCE;
}

std::vector<BvhLib::InstanceRecord> Stage::flatInstances() const
{
    std::vector<BvhLib::InstanceRecord> records;
    records.reserve(m_scene.instances.size());
    for (const BvhLib::Instance& instance : m_scene.instances) {
        records.push_back(BvhLib::flattenInstance(instance, m_node_offsets[instance.geometry], m_triangle_offsets[instance.geometry],
            static_cast<uint32_t>(m_scene.geometries[instance.geometry].nodes.size())));
    }
    return records;
}
