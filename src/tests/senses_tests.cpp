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

TEST_CASE(a_body_declaring_eyes_is_refused_rather_than_left_unseeing)
{
    const BvhLib::Scene scene{singingFloorScene()};
    GridSensesSource source{scene, unitStrengths()};

    RosterLib::Creature creature{hearingCreature(nullptr, 0u)};
    creature.body.eye_count = 1u;

    TglSenses senses{};
    TEST_CHECK_THROWS(source.fill(creature, senses));
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

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
