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

/*
    The live view's scene, checked without a device: what the tracer's dynamic instance path will
    be fed is plain records, and everything worth being wrong about them — where the placeholder
    geometry begins, which material it wears, where a pose puts its nose — is host arithmetic.
*/

#include "../roster.hpp"
#include "../stage.hpp"
#include "../world_stage.hpp"

#include <bvh/bvh.hpp>
#include <math/vector.hpp>

#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <testing/testing.hpp>

namespace
{

    //! A two-triangle stand-in for the Grid, wearing material slots 0 and 1.
    [[nodiscard]] BvhLib::Bvh makeLittleGrid()
    {
        std::vector<BvhLib::Triangle> triangles;
        triangles.push_back(BvhLib::Triangle{.v0 = MathLib::Vec3{-10.0f, 0.0f, -10.0f},
            .material = 0u,
            .edge1 = MathLib::Vec3{20.0f, 0.0f, 0.0f},
            .padding0 = 0u,
            .edge2 = MathLib::Vec3{0.0f, 0.0f, 20.0f},
            .padding1 = 0u});
        triangles.push_back(BvhLib::Triangle{.v0 = MathLib::Vec3{-10.0f, 0.0f, 10.0f},
            .material = 1u,
            .edge1 = MathLib::Vec3{20.0f, 0.0f, 0.0f},
            .padding0 = 0u,
            .edge2 = MathLib::Vec3{20.0f, 0.0f, -20.0f},
            .padding1 = 0u});
        return BvhLib::build(triangles);
    }

    [[nodiscard]] std::vector<Material> makeLittleMaterials()
    {
        return {makeMirror(MathLib::Vec3{0.9f, 0.9f, 0.9f}), makeEmissive(MathLib::Vec3{0.1f, 0.3f, 0.5f}, MathLib::Vec3{1.0f, 2.0f, 3.0f})};
    }

    //! A record's affine rows applied to a point: the arithmetic the shader performs.
    [[nodiscard]] MathLib::Vec3 applyRows(const MathLib::Vec4& row0, const MathLib::Vec4& row1, const MathLib::Vec4& row2, const MathLib::Vec3& point)
    {
        return MathLib::Vec3{(row0.x * point.x) + (row0.y * point.y) + (row0.z * point.z) + row0.w, (row1.x * point.x) + (row1.y * point.y) + (row1.z * point.z) + row1.w,
            (row2.x * point.x) + (row2.y * point.y) + (row2.z * point.z) + row2.w};
    }

}

TEST_CASE(the_empty_world_is_the_grid_alone)
{
    const WorldStageLib::WorldStage stage{makeLittleGrid(), makeLittleMaterials(), 4u};

    TEST_CHECK_EQUAL(stage.instanceCapacity(), 1u + (4u * LNK_SEGMENTS_MAX)); // A whole chain per creature.
    TEST_CHECK_EQUAL(stage.materials().size(), 3u);

    const std::vector<BvhLib::InstanceRecord> records{stage.records({})};
    TEST_CHECK_EQUAL(records.size(), 1u);
    TEST_CHECK_EQUAL(records.front().node_offset, 0u);
    TEST_CHECK(records.front().node_count > 0u);
}

TEST_CASE(a_creature_stands_in_the_placeholder_body_at_its_pose)
{
    const WorldStageLib::WorldStage stage{makeLittleGrid(), makeLittleMaterials(), 4u};

    WorldClientLib::InterpolatedCreature creature{};
    creature.creature_id = 7u;
    creature.position[0] = 2.0f;
    creature.position[1] = 0.05f;
    creature.position[2] = -3.0f;
    creature.yaw = 1.1f;

    const std::vector<BvhLib::InstanceRecord> records{stage.records({creature})};
    TEST_CHECK_EQUAL(records.size(), 2u);

    // The body's geometry begins where the Grid's ends in the concatenated buffers.
    const BvhLib::InstanceRecord& body{records[1]};
    TEST_CHECK_EQUAL(body.node_offset, records[0].node_count);
    TEST_CHECK(body.node_count > 0u);
    TEST_CHECK(body.triangle_offset > 0u);

    /*
        The record's to_world rows must put the body's nose exactly where `worldFromBody` puts it:
        the pose transform and the point-at-a-time rotation are twins, and this holds the flattened
        rows to the pair of them. The nose is -Z of the body because that is the way a body faces.
    */
    const RosterLib::Pose pose{.position = MathLib::Vec3{2.0f, 0.05f, -3.0f}, .yaw = 1.1f};
    const MathLib::Vec3 nose_body{0.0f, 0.0f, -RosterLib::BODY_HALF_LENGTH};
    const MathLib::Vec3 expected{RosterLib::worldFromBody(pose, nose_body)};
    const MathLib::Vec3 placed{applyRows(body.to_world_row0, body.to_world_row1, body.to_world_row2, nose_body)};
    TEST_CHECK_CLOSE(placed.x, expected.x, 1e-5f);
    TEST_CHECK_CLOSE(placed.y, expected.y, 1e-5f);
    TEST_CHECK_CLOSE(placed.z, expected.z, 1e-5f);

    // And to_instance is its inverse: out and back is where the point began.
    const MathLib::Vec3 returned{applyRows(body.to_instance_row0, body.to_instance_row1, body.to_instance_row2, placed)};
    TEST_CHECK_CLOSE(returned.x, nose_body.x, 1e-5f);
    TEST_CHECK_CLOSE(returned.y, nose_body.y, 1e-5f);
    TEST_CHECK_CLOSE(returned.z, nose_body.z, 1e-5f);
}

TEST_CASE(a_chain_is_a_record_per_segment_each_at_its_own_pose)
{
    const WorldStageLib::WorldStage stage{makeLittleGrid(), makeLittleMaterials(), 4u};

    WorldClientLib::InterpolatedCreature creature{};
    creature.creature_id = 7u;
    creature.position[0] = 2.0f;
    creature.position[1] = 0.05f;
    creature.position[2] = -3.0f;
    creature.yaw = 0.0f;
    creature.segment_count = 3u;
    creature.segments[0] = LnkSegmentPose{.position = {2.0f, 0.05f, -2.5f}, .yaw = 0.3f, .pitch = 0.0f};
    creature.segments[1] = LnkSegmentPose{.position = {2.2f, 0.05f, -2.0f}, .yaw = 0.6f, .pitch = 0.0f};

    const std::vector<BvhLib::InstanceRecord> records{stage.records({creature})};
    TEST_CHECK_EQUAL(records.size(), 4u); // The Grid, the head, two trailing segments.

    const MathLib::Vec3 nose_body{0.0f, 0.0f, -RosterLib::BODY_HALF_LENGTH};
    for (std::uint32_t segment{1u}; segment < 3u; ++segment) {
        const LnkSegmentPose& placed{creature.segments[segment - 1u]};
        const RosterLib::Pose pose{.position = MathLib::Vec3{placed.position[0], placed.position[1], placed.position[2]}, .yaw = placed.yaw};
        const MathLib::Vec3 expected{RosterLib::worldFromBody(pose, nose_body)};
        const BvhLib::InstanceRecord& record{records[1u + segment]};
        const MathLib::Vec3 got{applyRows(record.to_world_row0, record.to_world_row1, record.to_world_row2, nose_body)};
        TEST_CHECK_CLOSE(got.x, expected.x, 1e-5f);
        TEST_CHECK_CLOSE(got.z, expected.z, 1e-5f);
        // Every segment shares the head's geometry.
        TEST_CHECK_EQUAL(record.node_offset, records[1].node_offset);
    }

    // Four creatures of eight segments is the capacity; a fifth chain is refused, not truncated.
    std::vector<WorldClientLib::InterpolatedCreature> crowd(5u, creature);
    for (std::uint32_t index{0u}; index < crowd.size(); ++index) {
        crowd[index].creature_id = 10u + index;
        crowd[index].segment_count = LNK_SEGMENTS_MAX;
    }
    bool refused{false};
    try {
        static_cast<void>(stage.records(crowd));
    } catch (const std::runtime_error&) {
        refused = true;
    }
    TEST_CHECK(refused);
}

TEST_CASE(a_creature_whose_rez_carried_a_shape_wears_it_and_the_rest_keep_the_placeholder)
{
    WorldStageLib::WorldStage stage{makeLittleGrid(), makeLittleMaterials(), 4u};
    const std::size_t bare_materials{stage.materials().size()};
    const BvhLib::FlatScene bare{stage.flatScene()};

    // One shaped body (a single triangle, one material, emissive green) and one bodiless one.
    WorldClientLib::Body shaped{};
    shaped.rez.creature_id = 7u;
    shaped.rez.vertex_count = 3u;
    shaped.rez.triangle_count = 1u;
    shaped.rez.material_count = 1u;
    shaped.vertices = {LnkRezVertex{.position = {0.0f, 0.0f, 0.0f}}, LnkRezVertex{.position = {0.1f, 0.0f, 0.0f}}, LnkRezVertex{.position = {0.0f, 0.1f, -0.3f}}};
    shaped.triangles = {LnkRezTriangle{.vertices = {0u, 1u, 2u}, .material = 0u}};
    shaped.materials = {LnkRezMaterial{.colour = {0.1f, 0.2f, 0.3f}, .index_of_refraction = 1.5f, .emission = {0.0f, 4.0f, 0.0f}, .transmission = 0.0f}};
    WorldClientLib::Body bodiless{};
    bodiless.rez.creature_id = 9u;

    std::unordered_map<std::uint32_t, WorldClientLib::Body> bodies;
    bodies[7u] = shaped;
    bodies[9u] = bodiless;
    stage.setBodies(bodies);

    // The table grew by the shaped body's one material, mapped field for field; the scene grew
    // by one geometry, placed after the Grid and the placeholder in the concatenated buffers.
    TEST_CHECK_EQUAL(stage.materials().size(), bare_materials + 1u);
    TEST_CHECK_CLOSE(stage.materials().back().emission.y, 4.0f, 1e-6f);
    const BvhLib::FlatScene shaped_scene{stage.flatScene()};
    TEST_CHECK(shaped_scene.triangles.size() == bare.triangles.size() + 1u);
    TEST_CHECK_EQUAL(shaped_scene.triangles.back().material, static_cast<std::uint32_t>(bare_materials));

    WorldClientLib::InterpolatedCreature seven{};
    seven.creature_id = 7u;
    seven.position[1] = 0.05f;
    WorldClientLib::InterpolatedCreature nine{};
    nine.creature_id = 9u;
    nine.position[0] = 2.0f;
    const std::vector<BvhLib::InstanceRecord> records{stage.records({seven, nine})};
    TEST_CHECK_EQUAL(records.size(), 3u);
    // Seven's record names the new geometry: beyond the placeholder's nodes; nine's is the
    // placeholder's, exactly where a creature without a shape always stood.
    const std::uint32_t placeholder_offset{records[0].node_count};
    TEST_CHECK(records[1].node_offset > placeholder_offset);
    TEST_CHECK_EQUAL(records[1].triangle_offset, static_cast<std::uint32_t>(bare.triangles.size()));
    TEST_CHECK_EQUAL(records[2].node_offset, placeholder_offset);

    // Taking the shape away brings the stage back to exactly the bare scene.
    bodies.erase(7u);
    stage.setBodies(bodies);
    TEST_CHECK_EQUAL(stage.materials().size(), bare_materials);
    TEST_CHECK(stage.flatScene().triangles.size() == bare.triangles.size());
    TEST_CHECK_EQUAL(stage.records({seven})[1].node_offset, placeholder_offset);
}

TEST_CASE(the_placeholder_wears_its_own_material_slot)
{
    const WorldStageLib::WorldStage stage{makeLittleGrid(), makeLittleMaterials(), 4u};

    // The slot appended for the body is the one past the Grid's table, and every triangle past the
    // Grid's in the concatenated buffer must wear it: a body triangle indexing a Grid slot would
    // shade as floor, silently.
    const BvhLib::FlatScene flat{stage.flatScene()};
    const std::uint32_t body_material{2u};
    TEST_CHECK_EQUAL(flat.triangles.size(), 10u); // Two of the little Grid, eight of the dart.
    for (std::size_t index{2u}; index < flat.triangles.size(); ++index) {
        TEST_CHECK_EQUAL(flat.triangles[index].material, body_material);
    }
}

TEST_CASE(more_creatures_than_the_stage_was_built_for_is_refused)
{
    const WorldStageLib::WorldStage stage{makeLittleGrid(), makeLittleMaterials(), 2u};

    const std::vector<WorldClientLib::InterpolatedCreature> crowd(3u);
    try {
        const std::vector<BvhLib::InstanceRecord> records{stage.records(crowd)};
        TEST_CHECK(false);
    } catch (const std::runtime_error& error) {
        const std::string message{error.what()};
        TEST_CHECK(message.find("More creatures") != std::string::npos);
    }
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
