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

    TEST_CHECK_EQUAL(stage.instanceCapacity(), 5u);
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
