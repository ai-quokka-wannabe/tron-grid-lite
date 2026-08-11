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

#include "../stage.hpp"

#include <testing/testing.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

/*
    The stage is where an accepted model becomes something rays can strike, so what these hold it to
    is placement arithmetic: one material table with global indices, an instance that stands exactly
    where the pose says, and per-tick records that match the whole flatten to the byte.
*/

namespace
{

    //! A tiny flat "Grid" to stand on: one quad of material 0.
    [[nodiscard]] BvhLib::Bvh littleGrid()
    {
        const MathLib::Vec3 a{-5.0f, 0.0f, -5.0f};
        const MathLib::Vec3 b{5.0f, 0.0f, -5.0f};
        const MathLib::Vec3 c{5.0f, 0.0f, 5.0f};

        std::vector<BvhLib::Triangle> triangles;
        triangles.push_back(BvhLib::Triangle{.v0 = a, .material = 0u, .edge1 = b - a, .padding0 = 0u, .edge2 = c - a, .padding1 = 0u});
        return BvhLib::build(std::move(triangles));
    }

    //! One "grid" material, so the first creature slot lands at global index 1.
    [[nodiscard]] std::vector<Material> littleMaterials()
    {
        return {Material{}};
    }

    //! A creature with a two-triangle, two-material model: enough to watch every index travel.
    [[nodiscard]] RosterLib::Creature modelledCreature(uint64_t creature_id, const MathLib::Vec3& position, float yaw)
    {
        RosterLib::Creature creature{};
        creature.body.creature_id = creature_id;
        creature.pose.position = position;
        creature.pose.yaw = yaw;

        creature.model.vertex_positions = {MathLib::Vec3{-0.1f, 0.0f, -0.1f}, MathLib::Vec3{0.1f, 0.0f, -0.1f}, MathLib::Vec3{0.0f, 0.1f, 0.1f}};
        creature.model.triangles = {TglRenderTriangle{{0u, 1u, 2u}, 0u}, TglRenderTriangle{{2u, 1u, 0u}, 1u}};
        creature.model.materials = {
            TglRenderMaterial{{0.1f, 0.2f, 0.3f}, 1.5f, {0.0f, 0.0f, 0.0f}, 0.0f}, TglRenderMaterial{{0.0f, 0.0f, 0.0f}, 1.5f, {1.0f, 2.0f, 3.0f}, 0.0f}};

        return creature;
    }

} // namespace

TEST_CASE(a_bodiless_roster_stages_only_the_grid)
{
    std::vector<RosterLib::Creature> creatures(2u);
    creatures[0].body.creature_id = 7u;
    creatures[1].body.creature_id = 8u;

    Stage stage{littleGrid(), littleMaterials(), creatures};

    TEST_CHECK_EQUAL(stage.scene().geometries.size(), 1u);
    TEST_CHECK_EQUAL(stage.scene().instances.size(), 1u);
    TEST_CHECK_EQUAL(stage.materials().size(), 1u);
    TEST_CHECK_EQUAL(stage.instanceOf(7u), BvhLib::NO_INSTANCE);
    TEST_CHECK_EQUAL(stage.instanceOf(8u), BvhLib::NO_INSTANCE);
}

TEST_CASE(a_modelled_creature_stands_exactly_where_its_pose_says)
{
    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(modelledCreature(7u, MathLib::Vec3{2.0f, 1.0f, -3.0f}, 0.7f));

    Stage stage{littleGrid(), littleMaterials(), creatures};

    TEST_CHECK_EQUAL(stage.scene().geometries.size(), 2u);
    TEST_CHECK_EQUAL(stage.scene().instances.size(), 2u);
    TEST_CHECK_EQUAL(stage.instanceOf(7u), 1u);

    /*
        The transform twin: a body point carried by the instance's matrix must land exactly where
        `worldFromBody` carries it one point at a time, because the eyes ride the matrix and the
        ears ride the function, and an ear that disagreed with its own eye about where the body
        stands would be a defect no single sense could see.
    */
    const MathLib::Vec3 body_point{0.1f, 0.05f, -0.2f};
    const MathLib::Vec3 expected{RosterLib::worldFromBody(creatures[0].pose, body_point)};
    const MathLib::Vec4 carried{stage.scene().instances[1].to_world * MathLib::Vec4::fromVec3(body_point, 1.0f)};

    TEST_CHECK_CLOSE(carried.x, expected.x, 1e-5f);
    TEST_CHECK_CLOSE(carried.y, expected.y, 1e-5f);
    TEST_CHECK_CLOSE(carried.z, expected.z, 1e-5f);
}

TEST_CASE(materials_join_one_table_with_global_indices)
{
    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(modelledCreature(7u, MathLib::Vec3{}, 0.0f));

    Stage stage{littleGrid(), littleMaterials(), creatures};

    // One grid slot, then the body's two, in offer order.
    TEST_CHECK_EQUAL(stage.materials().size(), 3u);
    TEST_CHECK_CLOSE(stage.materials()[1].colour.x, 0.1f, 1e-6f);
    TEST_CHECK_CLOSE(stage.materials()[2].emission.z, 3.0f, 1e-6f);

    // The body's triangles carry the global slots, not the model's local ones.
    const BvhLib::Bvh& body{stage.scene().geometries[1]};
    TEST_CHECK_EQUAL(body.triangles.size(), 2u);
    for (const BvhLib::Triangle& triangle : body.triangles) {
        TEST_CHECK(triangle.material >= 1u);
        TEST_CHECK(triangle.material <= 2u);
    }

    // Every slot has an acoustic strength, and every body slot's is a written zero: a hull
    // reflects and does not sing.
    TEST_CHECK_EQUAL(stage.acousticStrengths().size(), 3u);
    TEST_CHECK_EQUAL(stage.acousticStrengths()[1], 0.0f);
    TEST_CHECK_EQUAL(stage.acousticStrengths()[2], 0.0f);
}

TEST_CASE(an_update_moves_the_instance_and_nothing_else)
{
    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(modelledCreature(7u, MathLib::Vec3{0.0f, 1.0f, 0.0f}, 0.0f));

    Stage stage{littleGrid(), littleMaterials(), creatures};
    const size_t nodes_before{stage.scene().geometries[1].nodes.size()};

    creatures[0].pose.position = MathLib::Vec3{4.0f, 1.0f, 2.0f};
    creatures[0].pose.yaw = 1.2f;
    stage.update(creatures);

    // The instance follows the pose; the geometry is never rebuilt.
    const MathLib::Vec3 body_point{0.0f, 0.1f, 0.1f};
    const MathLib::Vec3 expected{RosterLib::worldFromBody(creatures[0].pose, body_point)};
    const MathLib::Vec4 carried{stage.scene().instances[1].to_world * MathLib::Vec4::fromVec3(body_point, 1.0f)};
    TEST_CHECK_CLOSE(carried.x, expected.x, 1e-5f);
    TEST_CHECK_CLOSE(carried.y, expected.y, 1e-5f);
    TEST_CHECK_CLOSE(carried.z, expected.z, 1e-5f);

    TEST_CHECK_EQUAL(stage.scene().geometries[1].nodes.size(), nodes_before);
}

TEST_CASE(the_cheap_records_match_the_whole_flatten_to_the_byte)
{
    /*
        `flatInstances` exists so a tick can refresh placements without re-concatenating the world;
        this is the twin that keeps the cheap path honest. Byte equality rather than tolerance,
        because both paths are meant to run the very same arithmetic through `flattenInstance`.
    */
    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(modelledCreature(7u, MathLib::Vec3{1.0f, 2.0f, 3.0f}, 0.5f));
    creatures.push_back(modelledCreature(8u, MathLib::Vec3{-2.0f, 1.0f, 4.0f}, -1.1f));

    Stage stage{littleGrid(), littleMaterials(), creatures};

    creatures[0].pose.yaw = 2.2f;
    stage.update(creatures);

    const std::vector<BvhLib::InstanceRecord> cheap{stage.flatInstances()};
    const BvhLib::FlatScene whole{stage.flatten()};

    TEST_CHECK_EQUAL(cheap.size(), whole.instances.size());
    TEST_CHECK(std::memcmp(cheap.data(), whole.instances.data(), cheap.size() * sizeof(BvhLib::InstanceRecord)) == 0);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
