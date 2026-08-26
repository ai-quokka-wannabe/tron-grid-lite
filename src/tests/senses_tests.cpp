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

#include "../senses.hpp"

#include <testing/testing.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

/*
    The first tests where a creature perceives something of a Grid: a singing surface, two ears, and
    the gather answering through the senses seam. Still no Vulkan and still no device — hearing is a
    host answer, which is the property that lets these run on every push.
*/

namespace
{

    //! A small singing floor below the origin: two triangles of material 0, four metres square.
    [[nodiscard]] BvhLib::Scene singingFloorScene()
    {
        const MathLib::Vec3 a{-2.0f, 0.0f, -2.0f};
        const MathLib::Vec3 b{2.0f, 0.0f, -2.0f};
        const MathLib::Vec3 c{-2.0f, 0.0f, 2.0f};
        const MathLib::Vec3 d{2.0f, 0.0f, 2.0f};

        std::vector<BvhLib::Triangle> triangles;
        triangles.push_back(BvhLib::Triangle{.v0 = a, .material = 0u, .edge1 = c - a, .padding0 = 0u, .edge2 = b - a, .padding1 = 0u});
        triangles.push_back(BvhLib::Triangle{.v0 = b, .material = 0u, .edge1 = c - b, .padding0 = 0u, .edge2 = d - b, .padding1 = 0u});

        BvhLib::Bvh hierarchy{BvhLib::build(std::move(triangles))};

        BvhLib::Scene scene{};
        scene.instances.push_back(BvhLib::makeInstance(hierarchy, 0u, MathLib::Mat4::identity()));
        scene.geometries.push_back(std::move(hierarchy));
        return scene;
    }

    //! Every material in the tiny scene sings at unit strength.
    [[nodiscard]] std::vector<float> unitStrengths()
    {
        return {1.0f};
    }

    constexpr std::array<float, Acoustics::BAND_COUNT + 1u> BAND_EDGES_HZ{2000.0f, 4500.0f, 7500.0f, 10500.0f, 13500.0f};
    constexpr std::array<float, Acoustics::BAND_COUNT> NO_AIR_ABSORPTION{0.0f, 0.0f, 0.0f, 0.0f};

    [[nodiscard]] TglEarDesc earAt(float x, float y, float z)
    {
        return TglEarDesc{
            .band_edges_hz = BAND_EDGES_HZ.data(),
            .air_absorption_db_per_km = NO_AIR_ABSORPTION.data(),
            .position = {x, y, z},
            .band_count = Acoustics::BAND_COUNT,
            .bin_count = Acoustics::BIN_COUNT,
            .bin_seconds = Acoustics::BIN_SECONDS,
        };
    }

    //! A creature the tests build directly: the seam takes a Creature, not a Roster.
    [[nodiscard]] RosterLib::Creature hearingCreature(const TglEarDesc* ears, uint32_t ear_count)
    {
        RosterLib::Creature creature{};
        creature.body.creature_id = 7u;
        creature.body.ears = ears;
        creature.body.ear_count = ear_count;
        creature.pose.position = MathLib::Vec3{0.0f, 1.0f, 0.0f};
        return creature;
    }

    [[nodiscard]] float totalEnergy(const TglEarView& view)
    {
        float total{0.0f};
        for (uint32_t index{0u}; index < (view.band_count * view.bin_count); ++index) {
            total += view.energy[index];
        }
        return total;
    }

} // namespace

TEST_CASE(ears_are_filled_shaped_and_aligned)
{
    const BvhLib::Scene scene{singingFloorScene()};
    GridSensesSource source{scene, unitStrengths()};

    const std::array<TglEarDesc, 2u> ears{earAt(0.0f, 0.0f, -0.2f), earAt(0.0f, 0.0f, 0.2f)};
    const RosterLib::Creature creature{hearingCreature(ears.data(), 2u)};

    TglSenses senses{};
    source.fill(creature, senses);

    TEST_CHECK_EQUAL(senses.ear_count, 2u);
    TEST_CHECK(senses.ears != nullptr);

    for (uint32_t index{0u}; index < 2u; ++index) {
        const TglEarView& view{senses.ears[index]};

        // The repeated values the ABI promises are asserted against the descriptor as they are
        // filled, and the alignment promise TglEarView::energy documents.
        TEST_CHECK_EQUAL(view.band_count, Acoustics::BAND_COUNT);
        TEST_CHECK_EQUAL(view.bin_count, Acoustics::BIN_COUNT);
        TEST_CHECK(view.energy != nullptr);
        TEST_CHECK_EQUAL(reinterpret_cast<uintptr_t>(view.energy) % 16u, static_cast<uintptr_t>(0u));

        // The floor sings and the ear floats a metre above it, so silence here would mean the
        // gather was never asked — the comparison must have something to compare.
        TEST_CHECK(totalEnergy(view) > 0.0f);
    }
}

TEST_CASE(a_moved_creature_hears_a_different_grid)
{
    const BvhLib::Scene scene{singingFloorScene()};
    GridSensesSource source{scene, unitStrengths()};

    const std::array<TglEarDesc, 1u> ears{earAt(0.0f, 0.0f, 0.0f)};
    RosterLib::Creature creature{hearingCreature(ears.data(), 1u)};

    TglSenses near_senses{};
    source.fill(creature, near_senses);
    std::array<float, Acoustics::BAND_COUNT * Acoustics::BIN_COUNT> near_energy{};
    std::memcpy(near_energy.data(), near_senses.ears[0].energy, sizeof(near_energy));

    // Twice as far from the singing floor: energy arrives later and weaker, so the histograms must
    // differ — and if they do not, the cache failed to notice the world position changed.
    creature.pose.position = MathLib::Vec3{0.0f, 2.0f, 0.0f};
    TglSenses far_senses{};
    source.fill(creature, far_senses);

    TEST_CHECK(std::memcmp(near_energy.data(), far_senses.ears[0].energy, sizeof(near_energy)) != 0);
}

TEST_CASE(a_stationary_creature_hears_bit_identically)
{
    /*
        The gather is a pure function, so a stationary ear must read the same response to the last
        bit however many ticks pass — that is the licence for the keyed skip, and the property a
        recording's replay stands on.
    */
    const BvhLib::Scene scene{singingFloorScene()};
    GridSensesSource source{scene, unitStrengths()};

    const std::array<TglEarDesc, 1u> ears{earAt(0.0f, 0.0f, 0.0f)};
    const RosterLib::Creature creature{hearingCreature(ears.data(), 1u)};

    TglSenses first{};
    source.fill(creature, first);
    std::array<float, Acoustics::BAND_COUNT * Acoustics::BIN_COUNT> first_energy{};
    std::memcpy(first_energy.data(), first.ears[0].energy, sizeof(first_energy));

    TglSenses second{};
    source.fill(creature, second);

    TEST_CHECK(std::memcmp(first_energy.data(), second.ears[0].energy, sizeof(first_energy)) == 0);
}

TEST_CASE(a_body_declaring_eyes_is_refused_when_no_solver_is_attached)
{
    const BvhLib::Scene scene{singingFloorScene()};
    GridSensesSource source{scene, unitStrengths()};

    RosterLib::Creature creature{hearingCreature(nullptr, 0u)};
    creature.body.eye_count = 1u;

    TglSenses senses{};
    TEST_CHECK_THROWS(source.fill(creature, senses));
}

namespace
{

    //! Answers every ray from its direction alone, so a test can predict each sample exactly —
    //! and remembers the placements it was last staged with, so a test can watch the self blank.
    class FakeSolver final : public RadianceSolver {
    public:
        void stage(const std::vector<BvhLib::InstanceRecord>& instances) override
        {
            staged = instances;
        }

        [[nodiscard]] std::vector<MathLib::Vec4> solve(const std::vector<MathLib::Vec4>& rays, uint32_t) override
        {
            ++calls;
            std::vector<MathLib::Vec4> out;
            out.reserve(rays.size() / 2u);
            for (std::size_t index{0u}; index < rays.size(); index += 2u) {
                const MathLib::Vec4& direction{rays[index + 1u]};
                out.push_back(MathLib::Vec4{std::fabs(direction.x), std::fabs(direction.y), std::fabs(direction.z), 0.0f});
            }
            return out;
        }

        uint32_t calls{0u};
        std::vector<BvhLib::InstanceRecord> staged;
    };

    constexpr std::array<float, 3u> FORWARD_DIRECTION{0.0f, 0.0f, -1.0f};
    constexpr std::array<float, 3u> RIGHT_DIRECTION{1.0f, 0.0f, 0.0f};
    constexpr std::array<float, 1u> ONE_ACCEPTANCE{0.5f};

    [[nodiscard]] TglEyeDesc eyeOf(const float* directions, uint32_t sample_count, uint32_t channels)
    {
        return TglEyeDesc{
            .sample_directions = directions,
            .sample_acceptance_angles = ONE_ACCEPTANCE.data(),
            .position = {0.0f, 0.1f, -0.2f},
            .sample_count = sample_count,
            .channels = channels,
            .quantisation_bits = 0u,
        };
    }

} // namespace

TEST_CASE(eyes_and_irradiance_are_filled_from_the_solver)
{
    const BvhLib::Scene scene{singingFloorScene()};
    FakeSolver solver;
    GridSensesSource source{scene, unitStrengths(), {}, &solver};

    // One scalar eye looking forward, one three-band eye looking right, and four sphere samples.
    const std::array<TglEyeDesc, 2u> eyes{eyeOf(FORWARD_DIRECTION.data(), 1u, 1u), eyeOf(RIGHT_DIRECTION.data(), 1u, 3u)};

    RosterLib::Creature creature{hearingCreature(nullptr, 0u)};
    creature.body.eyes = eyes.data();
    creature.body.eye_count = 2u;
    creature.body.irradiance_sample_count = 4u;

    TglSenses senses{};
    source.fill(creature, senses);

    TEST_CHECK_EQUAL(senses.eye_count, 2u);
    TEST_CHECK(senses.eyes != nullptr);

    // The scalar eye weights the three bands equally: |0| + |0| + |-1| over three.
    const TglEyeView& scalar_eye{senses.eyes[0]};
    TEST_CHECK_EQUAL(scalar_eye.sample_count, 1u);
    TEST_CHECK_EQUAL(scalar_eye.channels, 1u);
    TEST_CHECK_EQUAL(reinterpret_cast<uintptr_t>(scalar_eye.samples) % 16u, static_cast<uintptr_t>(0u));
    TEST_CHECK_CLOSE(scalar_eye.samples[0], 1.0f / 3.0f, 1e-6f);

    // The three-band eye receives the solver's answer verbatim.
    const TglEyeView& banded_eye{senses.eyes[1]};
    TEST_CHECK_EQUAL(banded_eye.channels, 3u);
    TEST_CHECK_CLOSE(banded_eye.samples[0], 1.0f, 1e-6f);
    TEST_CHECK_CLOSE(banded_eye.samples[1], 0.0f, 1e-6f);
    TEST_CHECK_CLOSE(banded_eye.samples[2], 0.0f, 1e-6f);

    // Irradiance is the mean over the fixed Fibonacci set of the per-direction intensity.
    float expected{0.0f};
    for (uint32_t sample{0u}; sample < 4u; ++sample) {
        const MathLib::Vec3 direction{Acoustics::fibonacciDirection(sample, 4u)};
        expected += (std::fabs(direction.x) + std::fabs(direction.y) + std::fabs(direction.z)) / 3.0f;
    }
    TEST_CHECK_CLOSE(senses.irradiance, expected / 4.0f, 1e-5f);
}

TEST_CASE(the_solver_is_asked_once_while_the_pose_holds)
{
    /*
        The keyed skip, from the outside: a stationary creature's vision is a pure function of a
        pose that has not changed, so the second fill must reuse the first solve — and a moved
        creature must not.
    */
    const BvhLib::Scene scene{singingFloorScene()};
    FakeSolver solver;
    GridSensesSource source{scene, unitStrengths(), {}, &solver};

    const std::array<TglEyeDesc, 1u> eyes{eyeOf(FORWARD_DIRECTION.data(), 1u, 1u)};

    RosterLib::Creature creature{hearingCreature(nullptr, 0u)};
    creature.body.eyes = eyes.data();
    creature.body.eye_count = 1u;

    TglSenses first{};
    source.fill(creature, first);
    TglSenses second{};
    source.fill(creature, second);
    TEST_CHECK_EQUAL(solver.calls, 1u);

    creature.pose.yaw = 0.5f;
    TglSenses third{};
    source.fill(creature, third);
    TEST_CHECK_EQUAL(solver.calls, 2u);
}

TEST_CASE(an_ear_asking_for_a_shape_the_gather_does_not_produce_is_refused)
{
    const BvhLib::Scene scene{singingFloorScene()};
    GridSensesSource source{scene, unitStrengths()};

    TglEarDesc mismatched{earAt(0.0f, 0.0f, 0.0f)};
    mismatched.bin_count = Acoustics::BIN_COUNT / 2u;

    const RosterLib::Creature creature{hearingCreature(&mismatched, 1u)};

    TglSenses senses{};
    TEST_CHECK_THROWS(source.fill(creature, senses));
}

// ---------------------------------------------------------------------------------------------
// Calls through the seam: a vocalisation lands in every ear, on top of the hum
// ---------------------------------------------------------------------------------------------

namespace
{

    //! Returns the bin a path of the given length lands in.
    [[nodiscard]] uint32_t callBinOf(float path_metres)
    {
        return static_cast<uint32_t>(path_metres / (Acoustics::SPEED_OF_SOUND * Acoustics::BIN_SECONDS));
    }

} // namespace

TEST_CASE(a_call_reaches_every_ear_and_the_caller_hears_itself_first)
{
    const BvhLib::Scene scene{singingFloorScene()};
    GridSensesSource source{scene, unitStrengths()};

    const std::array<TglEarDesc, 1u> ears{earAt(0.0f, 0.0f, 0.0f)};

    // The caller stands at the origin's station; the listener 3.6 m away — bin 10 at 343 m/s.
    RosterLib::Creature caller{hearingCreature(ears.data(), 1u)};
    caller.body.creature_id = 7u;
    RosterLib::Creature listener{hearingCreature(ears.data(), 1u)};
    listener.body.creature_id = 8u;
    listener.pose.position = MathLib::Vec3{3.6f, 1.0f, 0.0f};

    // The hum alone, before anything sounds.
    TglSenses quiet{};
    source.fill(listener, quiet);
    std::array<float, Acoustics::BAND_COUNT * Acoustics::BIN_COUNT> hum_only{};
    std::memcpy(hum_only.data(), quiet.ears[0].energy, sizeof(hum_only));

    // Physics would have set this from the staged action; the tests are the physics here.
    caller.vocalisation = 1.0f;
    std::vector<RosterLib::Creature> roster;
    roster.push_back(caller);
    roster.push_back(listener);
    source.beginTick(roster);

    // The listener's ear reads the hum plus exactly one new arrival, in the bin the distance
    // dictates. Everything outside that bin is untouched, which is the additivity claim.
    TglSenses heard{};
    source.fill(listener, heard);
    const uint32_t direct_bin{callBinOf(3.6f)};
    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
        const uint32_t index{(band * Acoustics::BIN_COUNT) + direct_bin};
        TEST_CHECK_CLOSE(heard.ears[0].energy[index] - hum_only[index], 1.0f / (3.6f * 3.6f), 1e-6f);
    }
    for (uint32_t index{0u}; index < hum_only.size(); ++index) {
        if ((index % Acoustics::BIN_COUNT) != direct_bin) {
            TEST_CHECK(heard.ears[0].energy[index] == hum_only[index]);
        }
    }

    // The caller's own ear sits on the source, so its call arrives in bin zero at full strength —
    // hearing yourself speak is the proprioception the voice gets. Bin zero is the call alone: the
    // nearest singing surface is a full metre below, which is bin two, so nothing of the hum can
    // stand in the call's bin and the equality is exact.
    TglSenses own{};
    source.fill(caller, own);
    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
        TEST_CHECK(own.ears[0].energy[band * Acoustics::BIN_COUNT] == 1.0f);
    }
}

TEST_CASE(a_standing_body_blocks_the_hum_and_a_moving_one_stales_the_cache)
{
    /*
        Two claims in one scene, because the second is only observable through the first. A body
        standing between an ear and the singing floor shadows the hum — occlusion is most of what
        a body is for. And when that body moves, a *stationary* listener must hear the change: the
        hum cache keys on the listener's own pose, so without the bodies-moved staleness it would
        happily replay a world that no longer exists.
    */
    const MathLib::Vec3 a{-2.0f, 0.0f, -2.0f};
    const MathLib::Vec3 b{2.0f, 0.0f, -2.0f};
    const MathLib::Vec3 c{-2.0f, 0.0f, 2.0f};
    const MathLib::Vec3 d{2.0f, 0.0f, 2.0f};
    std::vector<BvhLib::Triangle> floor_triangles;
    floor_triangles.push_back(BvhLib::Triangle{.v0 = a, .material = 0u, .edge1 = c - a, .padding0 = 0u, .edge2 = b - a, .padding1 = 0u});
    floor_triangles.push_back(BvhLib::Triangle{.v0 = b, .material = 0u, .edge1 = c - b, .padding0 = 0u, .edge2 = d - b, .padding1 = 0u});

    const std::array<TglEarDesc, 1u> ears{earAt(0.0f, 0.0f, 0.0f)};
    RosterLib::Creature listener{hearingCreature(ears.data(), 1u)};

    // The blocker's body is a broad horizontal shade, wider than the floor below it: rezzed far
    // away first, so the baseline is an unshaded hum.
    RosterLib::Creature blocker{};
    blocker.body.creature_id = 9u;
    blocker.pose.position = MathLib::Vec3{100.0f, 0.5f, 0.0f};
    blocker.model.vertex_positions = {
        MathLib::Vec3{-3.0f, 0.0f, -3.0f}, MathLib::Vec3{3.0f, 0.0f, -3.0f}, MathLib::Vec3{-3.0f, 0.0f, 3.0f}, MathLib::Vec3{3.0f, 0.0f, 3.0f}};
    blocker.model.triangles = {TglRenderTriangle{{0u, 2u, 1u}, 0u}, TglRenderTriangle{{1u, 2u, 3u}, 0u}};
    blocker.model.materials = {TglRenderMaterial{{0.0f, 0.0f, 0.0f}, 1.5f, {0.0f, 0.0f, 0.0f}, 0.0f}};

    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(listener);
    creatures.push_back(blocker);

    Stage stage{BvhLib::build(std::move(floor_triangles)), {Material{}}, creatures};
    GridSensesSource source{stage.scene(), unitStrengths(), {}, nullptr, &stage};

    source.beginTick(creatures);
    TglSenses clear_sky{};
    source.fill(creatures[0], clear_sky);
    std::array<float, Acoustics::BAND_COUNT * Acoustics::BIN_COUNT> baseline{};
    std::memcpy(baseline.data(), clear_sky.ears[0].energy, sizeof(baseline));
    TEST_CHECK(totalEnergy(clear_sky.ears[0]) > 0.0f); // The comparison has something to compare.

    // The blocker slides under the listener. Every path from ear to floor now crosses its body.
    creatures[1].pose.position = MathLib::Vec3{0.0f, 0.5f, 0.0f};
    source.beginTick(creatures);
    TglSenses shaded{};
    source.fill(creatures[0], shaded);
    TEST_CHECK(totalEnergy(shaded.ears[0]) == 0.0f);

    // And away again: bit-identical to the baseline, or the staleness handling re-solved wrongly.
    creatures[1].pose.position = MathLib::Vec3{100.0f, 0.5f, 0.0f};
    source.beginTick(creatures);
    TglSenses cleared{};
    source.fill(creatures[0], cleared);
    TEST_CHECK(std::memcmp(baseline.data(), cleared.ears[0].energy, sizeof(baseline)) == 0);
}

TEST_CASE(a_guests_body_shades_the_hum_and_a_guests_call_is_heard)
{
    /*
        The other bodies in the world, as a creature host meets them: shaped by their own hosts'
        REZ and placed by the world's telling. The same claims as the hosted blocker's - a guest
        standing between an ear and the singing floor shadows the hum, a guest that moves stales
        the cache - and one more: a guest that calls is heard from where the world says it stands.
    */
    const MathLib::Vec3 a{-2.0f, 0.0f, -2.0f};
    const MathLib::Vec3 b{2.0f, 0.0f, -2.0f};
    const MathLib::Vec3 c{-2.0f, 0.0f, 2.0f};
    const MathLib::Vec3 d{2.0f, 0.0f, 2.0f};
    std::vector<BvhLib::Triangle> floor_triangles;
    floor_triangles.push_back(BvhLib::Triangle{.v0 = a, .material = 0u, .edge1 = c - a, .padding0 = 0u, .edge2 = b - a, .padding1 = 0u});
    floor_triangles.push_back(BvhLib::Triangle{.v0 = b, .material = 0u, .edge1 = c - b, .padding0 = 0u, .edge2 = d - b, .padding1 = 0u});

    const std::array<TglEarDesc, 1u> ears{earAt(0.0f, 0.0f, 0.0f)};
    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(hearingCreature(ears.data(), 1u));

    Stage stage{BvhLib::build(std::move(floor_triangles)), {Material{}}, creatures};

    // The guest's shape, exactly as REZ relays it: a broad horizontal shade, wider than the floor.
    WorldClientLib::Body shade{};
    shade.rez.creature_id = 777u;
    shade.rez.vertex_count = 4u;
    shade.rez.triangle_count = 2u;
    shade.rez.material_count = 1u;
    shade.vertices = {LnkRezVertex{.position = {-3.0f, 0.0f, -3.0f}}, LnkRezVertex{.position = {3.0f, 0.0f, -3.0f}}, LnkRezVertex{.position = {-3.0f, 0.0f, 3.0f}},
        LnkRezVertex{.position = {3.0f, 0.0f, 3.0f}}};
    shade.triangles = {LnkRezTriangle{.vertices = {0u, 2u, 1u}, .material = 0u}, LnkRezTriangle{.vertices = {1u, 2u, 3u}, .material = 0u}};
    shade.materials = {LnkRezMaterial{.colour = {0.0f, 0.0f, 0.0f}, .index_of_refraction = 1.5f, .emission = {0.0f, 0.0f, 0.0f}, .transmission = 0.0f}};
    std::unordered_map<uint32_t, WorldClientLib::Body> bodies;
    bodies[777u] = shade;
    stage.setGuests(bodies);
    TEST_CHECK(!stage.guestInstanceOf(777u).empty());
    TEST_CHECK(stage.guestInstanceOf(778u).empty());

    GridSensesSource source{stage.scene(), unitStrengths(), {}, nullptr, &stage};

    // Far away first: the baseline is an unshaded hum.
    source.tellGuests({Stage::GuestTelling{.creature_id = 777u, .pose = {.position = MathLib::Vec3{100.0f, 0.5f, 0.0f}, .yaw = 0.0f}, .vocalisation = 0.0f}});
    source.beginTick(creatures);
    TglSenses clear_sky{};
    source.fill(creatures[0], clear_sky);
    std::array<float, Acoustics::BAND_COUNT * Acoustics::BIN_COUNT> baseline{};
    std::memcpy(baseline.data(), clear_sky.ears[0].energy, sizeof(baseline));
    TEST_CHECK(totalEnergy(clear_sky.ears[0]) > 0.0f);

    // The guest slides under the listener: every path from ear to floor crosses its body.
    source.tellGuests({Stage::GuestTelling{.creature_id = 777u, .pose = {.position = MathLib::Vec3{0.0f, 0.5f, 0.0f}, .yaw = 0.0f}, .vocalisation = 0.0f}});
    source.beginTick(creatures);
    TglSenses shaded{};
    source.fill(creatures[0], shaded);
    TEST_CHECK(totalEnergy(shaded.ears[0]) == 0.0f);

    // Away again, and calling from 3.6 m: the baseline back bit for bit, plus one arrival in the
    // bin the distance dictates - the guest's voice, heard from where the world placed it.
    source.tellGuests({Stage::GuestTelling{.creature_id = 777u, .pose = {.position = MathLib::Vec3{100.0f, 0.5f, 0.0f}, .yaw = 0.0f}, .vocalisation = 0.0f}});
    source.beginTick(creatures);
    TglSenses cleared{};
    source.fill(creatures[0], cleared);
    TEST_CHECK(std::memcmp(baseline.data(), cleared.ears[0].energy, sizeof(baseline)) == 0);

    source.tellGuests({Stage::GuestTelling{.creature_id = 777u, .pose = {.position = MathLib::Vec3{100.0f, 0.5f, 0.0f}, .yaw = 0.0f}, .vocalisation = 0.0f},
        Stage::GuestTelling{.creature_id = 778u, .pose = {.position = MathLib::Vec3{3.6f, 1.0f, 0.0f}, .yaw = 0.0f}, .vocalisation = 1.0f}});
    source.beginTick(creatures);
    TglSenses heard{};
    source.fill(creatures[0], heard);
    const uint32_t direct_bin{callBinOf(3.6f)};
    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
        const uint32_t index{(band * Acoustics::BIN_COUNT) + direct_bin};
        TEST_CHECK_CLOSE(heard.ears[0].energy[index] - baseline[index], 1.0f / (3.6f * 3.6f), 1e-6f);
    }

    // Taking the guests away restores the hosted scene exactly: the stage forgets their shapes.
    stage.setGuests({});
    TEST_CHECK(stage.guestInstanceOf(777u).empty());
    TEST_CHECK_EQUAL(stage.scene().geometries.size(), static_cast<size_t>(1u));
}

TEST_CASE(a_caller_due_east_arrives_at_the_east_ear_first_by_the_interaural_distance_and_its_recession_is_heard)
{
    /*
        Etape 17's own acceptance: two ears ten centimetres apart, a caller due east. The east
        ear hears the onset first, by exactly the interaural distance over the speed of sound -
        sign and all - which is the sub-millisecond structure the histogram destroys and the
        arrival record keeps. A caller walking away carries a positive radial velocity; one
        walking in, negative; a silent tick carries no arrivals at all.
    */
    const BvhLib::Scene scene{singingFloorScene()};
    GridSensesSource source{scene, unitStrengths()};

    constexpr float HALF_INTERAURAL{0.05f};
    const std::array<TglEarDesc, 2u> ears{earAt(-HALF_INTERAURAL, 0.0f, 0.0f), earAt(HALF_INTERAURAL, 0.0f, 0.0f)};
    RosterLib::Creature listener{hearingCreature(ears.data(), 2u)};
    listener.body.creature_id = 8u;

    // The hum alone: no discrete arrival, the pointer null.
    TglSenses quiet{};
    source.fill(listener, quiet);
    TEST_CHECK_EQUAL(quiet.ears[0].arrival_count, 0u);
    TEST_CHECK(quiet.ears[0].arrivals == nullptr);

    RosterLib::Creature caller{hearingCreature(ears.data(), 2u)};
    caller.body.creature_id = 7u;
    caller.pose.position = MathLib::Vec3{3.6f, 1.0f, 0.0f}; // due east, 3.6 m
    caller.vocalisation = 1.0f;
    caller.velocity = MathLib::Vec3{1.0f, 0.0f, 0.0f}; // walking away, east

    std::vector<RosterLib::Creature> roster;
    roster.push_back(caller);
    roster.push_back(listener);
    source.beginTick(roster);
    TglSenses heard{};
    source.fill(listener, heard);

    TEST_CHECK(heard.ears[0].arrival_count >= 1u);
    TEST_CHECK(heard.ears[1].arrival_count >= 1u);
    // The direct path is delivered first: arrival zero at each ear.
    const TglArrival& west{heard.ears[0].arrivals[0]};
    const TglArrival& east{heard.ears[1].arrivals[0]};
    const float interaural_seconds{(2.0f * HALF_INTERAURAL) / Acoustics::SPEED_OF_SOUND};
    TEST_CHECK_CLOSE(west.onset_seconds - east.onset_seconds, interaural_seconds, 1e-6f);
    TEST_CHECK(east.onset_seconds < west.onset_seconds);
    TEST_CHECK_CLOSE(east.onset_seconds, (3.6f - HALF_INTERAURAL) / Acoustics::SPEED_OF_SOUND, 1e-6f);
    TEST_CHECK_CLOSE(east.radial_velocity, 1.0f, 1e-5f); // receding: positive
    TEST_CHECK(east.energy[0] > 0.0f);
    TEST_CHECK_CLOSE(east.energy[0], 1.0f / ((3.6f - HALF_INTERAURAL) * (3.6f - HALF_INTERAURAL)), 1e-6f);

    // Walking in: negative. And a caller crossing sideways: no radial motion at all.
    roster[0].velocity = MathLib::Vec3{-2.0f, 0.0f, 0.0f};
    source.beginTick(roster);
    TglSenses approaching{};
    source.fill(listener, approaching);
    TEST_CHECK_CLOSE(approaching.ears[1].arrivals[0].radial_velocity, -2.0f, 1e-5f);
    roster[0].velocity = MathLib::Vec3{0.0f, 0.0f, 3.0f};
    source.beginTick(roster);
    TglSenses crossing{};
    source.fill(listener, crossing);
    TEST_CHECK(std::fabs(crossing.ears[1].arrivals[0].radial_velocity) < 1e-5f);

    // The listener's own motion counts: both walking east together, nothing recedes.
    roster[0].velocity = MathLib::Vec3{1.0f, 0.0f, 0.0f};
    roster[1].velocity = MathLib::Vec3{1.0f, 0.0f, 0.0f};
    source.beginTick(roster);
    TglSenses together{};
    source.fill(roster[1], together);
    TEST_CHECK(std::fabs(together.ears[1].arrivals[0].radial_velocity) < 1e-5f);
}

TEST_CASE(a_creatures_own_body_is_blanked_for_its_own_eyes)
{
    // The device-side spelling of the self skip: the records handed to the solver carry the whole
    // roster, with the looking creature's own body at a zero node count — the flat form of "not
    // there" the shader already honours, so no shader ever learns a special case.
    RosterLib::Creature seer{};
    seer.body.creature_id = 7u;
    seer.pose.position = MathLib::Vec3{0.0f, 1.0f, 0.0f};
    seer.model.vertex_positions = {MathLib::Vec3{-0.1f, 0.0f, -0.1f}, MathLib::Vec3{0.1f, 0.0f, -0.1f}, MathLib::Vec3{0.0f, 0.1f, 0.1f}};
    seer.model.triangles = {TglRenderTriangle{{0u, 1u, 2u}, 0u}};
    seer.model.materials = {TglRenderMaterial{{0.1f, 0.1f, 0.1f}, 1.5f, {0.0f, 0.0f, 0.0f}, 0.0f}};

    const std::array<TglEyeDesc, 1u> eyes{eyeOf(FORWARD_DIRECTION.data(), 1u, 1u)};
    seer.body.eyes = eyes.data();
    seer.body.eye_count = 1u;

    std::vector<RosterLib::Creature> creatures;
    creatures.push_back(seer);

    const MathLib::Vec3 a{-2.0f, 0.0f, -2.0f};
    const MathLib::Vec3 b{2.0f, 0.0f, -2.0f};
    const MathLib::Vec3 c{-2.0f, 0.0f, 2.0f};
    std::vector<BvhLib::Triangle> floor_triangles;
    floor_triangles.push_back(BvhLib::Triangle{.v0 = a, .material = 0u, .edge1 = c - a, .padding0 = 0u, .edge2 = b - a, .padding1 = 0u});

    Stage stage{BvhLib::build(std::move(floor_triangles)), {Material{}}, creatures};
    FakeSolver solver;
    GridSensesSource source{stage.scene(), unitStrengths(), {}, &solver, &stage};

    source.beginTick(creatures);
    TglSenses senses{};
    source.fill(creatures[0], senses);

    TEST_CHECK_EQUAL(solver.staged.size(), 2u);
    TEST_CHECK(solver.staged[0].node_count > 0u); // The Grid stands.
    TEST_CHECK_EQUAL(solver.staged[1].node_count, 0u); // The seer's own body does not, to the seer.
}

TEST_CASE(the_hum_cache_survives_a_call_and_a_silent_tick_reads_pure_hum)
{
    const BvhLib::Scene scene{singingFloorScene()};
    GridSensesSource source{scene, unitStrengths()};

    const std::array<TglEarDesc, 1u> ears{earAt(0.0f, 0.0f, 0.0f)};
    RosterLib::Creature creature{hearingCreature(ears.data(), 1u)};

    // The hum alone.
    TglSenses before{};
    source.fill(creature, before);
    std::array<float, Acoustics::BAND_COUNT * Acoustics::BIN_COUNT> hum_only{};
    std::memcpy(hum_only.data(), before.ears[0].energy, sizeof(hum_only));

    // A tick with a call: the delivered response must differ — the comparison has to have
    // something to compare — but the cache underneath it must not learn the call.
    creature.vocalisation = 0.5f;
    std::vector<RosterLib::Creature> roster;
    roster.push_back(creature);
    source.beginTick(roster);

    TglSenses during{};
    source.fill(creature, during);
    TEST_CHECK(std::memcmp(hum_only.data(), during.ears[0].energy, sizeof(hum_only)) != 0);

    // A silent tick after: bit-identical to the original hum, or the call leaked into the cache
    // and a stationary ear would replay it for ever.
    creature.vocalisation = 0.0f;
    roster.clear();
    roster.push_back(creature);
    source.beginTick(roster);

    TglSenses after{};
    source.fill(creature, after);
    TEST_CHECK(std::memcmp(hum_only.data(), after.ears[0].energy, sizeof(hum_only)) == 0);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
