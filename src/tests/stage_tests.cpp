/*
    Copyright (C) 2026 The AiQuokkaWannabe Authors https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#include "../orbs.hpp"
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

    // The Grid, and the orbs' shapes waiting for food: geometry, never an instance until told.
    TEST_CHECK_EQUAL(stage.scene().geometries.size(), 1u + OrbLib::GLOW_LEVELS);
    TEST_CHECK_EQUAL(stage.scene().instances.size(), 1u);
    TEST_CHECK_EQUAL(stage.materials().size(), 1u + OrbLib::GLOW_LEVELS);
    TEST_CHECK(stage.instanceOf(7u).empty());
    TEST_CHECK(stage.instanceOf(8u).empty());
}

TEST_CASE(a_modelled_creature_stands_exactly_where_its_pose_says)
{
    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(modelledCreature(7u, MathLib::Vec3{2.0f, 1.0f, -3.0f}, 0.7f));

    Stage stage{littleGrid(), littleMaterials(), creatures};

    TEST_CHECK_EQUAL(stage.scene().geometries.size(), 2u + OrbLib::GLOW_LEVELS);
    TEST_CHECK_EQUAL(stage.scene().instances.size(), 2u);
    TEST_CHECK_EQUAL(stage.instanceOf(7u).first, 1u);
    TEST_CHECK_EQUAL(stage.instanceOf(7u).count, 1u);

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

TEST_CASE(a_chain_stands_in_consecutive_instances_of_one_geometry_and_its_trail_is_placed)
{
    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(modelledCreature(7u, MathLib::Vec3{2.0f, 1.0f, -3.0f}, 0.0f));
    creatures[0].model.segment_count = 3u;
    creatures[0].model.segment_spacing = 0.5f;

    Stage stage{littleGrid(), littleMaterials(), creatures};

    // One geometry for the whole chain; three placements after the Grid's, one per segment.
    TEST_CHECK_EQUAL(stage.scene().geometries.size(), 2u + OrbLib::GLOW_LEVELS);
    TEST_CHECK_EQUAL(stage.scene().instances.size(), 4u);
    const BvhLib::InstanceRange own{stage.instanceOf(7u)};
    TEST_CHECK_EQUAL(own.first, 1u);
    TEST_CHECK_EQUAL(own.count, 3u);
    TEST_CHECK(own.contains(1u) && own.contains(3u) && !own.contains(0u) && !own.contains(4u));
    for (uint32_t instance{1u}; instance <= 3u; ++instance) {
        TEST_CHECK_EQUAL(stage.scene().instances[instance].geometry, 1u);
    }

    // The world tells the trail: each trailing segment stands exactly at its own pose.
    creatures[0].trail = {
        RosterLib::Pose{.position = MathLib::Vec3{2.0f, 1.0f, -2.5f}, .yaw = 0.2f}, RosterLib::Pose{.position = MathLib::Vec3{2.1f, 1.0f, -2.0f}, .yaw = 0.4f}};
    stage.update(creatures);
    const MathLib::Vec3 body_point{0.1f, 0.05f, -0.2f};
    for (uint32_t segment{1u}; segment < 3u; ++segment) {
        const MathLib::Vec3 expected{RosterLib::worldFromBody(creatures[0].trail[segment - 1u], body_point)};
        const MathLib::Vec4 carried{stage.scene().instances[1u + segment].to_world * MathLib::Vec4::fromVec3(body_point, 1.0f)};
        TEST_CHECK_CLOSE(carried.x, expected.x, 1e-5f);
        TEST_CHECK_CLOSE(carried.y, expected.y, 1e-5f);
        TEST_CHECK_CLOSE(carried.z, expected.z, 1e-5f);
    }
    // And the head stayed where its pose says.
    const MathLib::Vec3 head{RosterLib::worldFromBody(creatures[0].pose, body_point)};
    const MathLib::Vec4 carried_head{stage.scene().instances[1].to_world * MathLib::Vec4::fromVec3(body_point, 1.0f)};
    TEST_CHECK_CLOSE(carried_head.z, head.z, 1e-5f);
}

TEST_CASE(materials_join_one_table_with_global_indices)
{
    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(modelledCreature(7u, MathLib::Vec3{}, 0.0f));

    Stage stage{littleGrid(), littleMaterials(), creatures};

    // One grid slot, then the body's two, in offer order, then the orbs' glow levels.
    TEST_CHECK_EQUAL(stage.materials().size(), 3u + OrbLib::GLOW_LEVELS);
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
    // reflects and does not sing, and neither does an orb: it glows, silently.
    TEST_CHECK_EQUAL(stage.acousticStrengths().size(), 3u + OrbLib::GLOW_LEVELS);
    TEST_CHECK_EQUAL(stage.acousticStrengths()[1], 0.0f);
    TEST_CHECK_EQUAL(stage.acousticStrengths()[2], 0.0f);
    TEST_CHECK_EQUAL(stage.acousticStrengths().back(), 0.0f);
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

TEST_CASE(every_orb_told_stands_in_the_scene_and_a_gone_one_leaves_it)
{
    /*
        The creature host's half of the orbs: the same shapes the spectator draws, standing in
        the very scene the hosted eyes trace and the ears walk, so an orb is seen by its glow and
        heard as an occluder with nothing special-cased. One instance per orb told, after every
        body; none for an orb the telling no longer holds.
    */
    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(modelledCreature(7u, MathLib::Vec3{0.0f, 1.0f, 0.0f}, 0.0f));
    Stage stage{littleGrid(), littleMaterials(), creatures};
    const size_t bodies_only{stage.scene().instances.size()};
    TEST_CHECK(stage.foodInstances().empty());

    const LnkFoodState whole{.food_id = 1u, .position = {3.0f, 0.25f, -2.0f}, .radius = 0.25f, .quantity = 1.0f};
    const LnkFoodState crumb{.food_id = 2u, .position = {-1.0f, 0.5f, 1.0f}, .radius = 0.5f, .quantity = 0.05f};
    stage.placeFood({whole, crumb});
    TEST_CHECK_EQUAL(stage.scene().instances.size(), bodies_only + 2u);
    TEST_CHECK_EQUAL(stage.foodInstances().first, static_cast<uint32_t>(bodies_only));
    TEST_CHECK_EQUAL(stage.foodInstances().count, 2u);

    // At its centre, sized to its radius: the unit sphere's top lands a radius above the centre.
    const MathLib::Vec4 top{stage.scene().instances[bodies_only + 1u].to_world * MathLib::Vec4::fromVec3(MathLib::Vec3{0.0f, 1.0f, 0.0f}, 1.0f)};
    TEST_CHECK_CLOSE(top.x, -1.0f, 1e-5f);
    TEST_CHECK_CLOSE(top.y, 1.0f, 1e-5f);
    TEST_CHECK_CLOSE(top.z, 1.0f, 1e-5f);

    // Each in the shape of its glow: the whole orb bright white, the crumb at the faintest step.
    const auto emission_of = [&stage](const size_t instance) {
        const BvhLib::Bvh& shape{stage.scene().geometries[stage.scene().instances[instance].geometry]};
        return stage.materials()[shape.triangles.front().material].emission;
    };
    TEST_CHECK_CLOSE(emission_of(bodies_only).y, OrbLib::FULL_EMISSION, 1e-6f);
    TEST_CHECK_CLOSE(emission_of(bodies_only + 1u).y, OrbLib::FULL_EMISSION / static_cast<float>(OrbLib::GLOW_LEVELS), 1e-6f);

    // The crumb is eaten: one orb stands. Then the larder is empty: none do.
    stage.placeFood({whole});
    TEST_CHECK_EQUAL(stage.scene().instances.size(), bodies_only + 1u);
    stage.placeFood({});
    TEST_CHECK_EQUAL(stage.scene().instances.size(), bodies_only);
    TEST_CHECK(stage.foodInstances().empty());

    // A guest arriving keeps the orbs standing, after its own instances.
    stage.placeFood({whole});
    WorldClientLib::Body guest{};
    guest.rez.creature_id = 777u;
    guest.rez.segment_count = 1u;
    guest.vertices = {LnkRezVertex{.position = {0.0f, 0.0f, 0.0f}}, LnkRezVertex{.position = {0.1f, 0.0f, 0.0f}}, LnkRezVertex{.position = {0.0f, 0.1f, 0.0f}}};
    guest.triangles = {LnkRezTriangle{.vertices = {0u, 1u, 2u}, .material = 0u}};
    guest.materials = {LnkRezMaterial{.colour = {0.0f, 0.0f, 0.0f}, .index_of_refraction = 1.5f, .emission = {0.0f, 0.0f, 0.0f}, .transmission = 0.0f}};
    std::unordered_map<uint32_t, WorldClientLib::Body> guests;
    guests[777u] = guest;
    stage.setGuests(guests);
    TEST_CHECK_EQUAL(stage.scene().instances.size(), bodies_only + 2u);
    TEST_CHECK_EQUAL(stage.foodInstances().first, static_cast<uint32_t>(bodies_only + 1u));
    TEST_CHECK_EQUAL(stage.foodInstances().count, 1u);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
