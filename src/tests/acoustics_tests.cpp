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

/*
    The acceptance criteria docs/ACOUSTICS.md sets for the gather, and nothing else.

    None of these needs a reference implementation from the literature, which is the point: the
    model is small enough that its own arithmetic is the specification. A test that agreed with a
    second implementation of the same misunderstanding would prove nothing.
*/

#include "../acoustics.hpp"
#include <testing/testing.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

    //! Appends a large axis-aligned horizontal quad at the given height, as two triangles.
    void appendHorizontalQuad(std::vector<BvhLib::Triangle>& out, float height, float half_extent, uint32_t material)
    {
        const MathLib::Vec3 a{-half_extent, height, -half_extent};
        const MathLib::Vec3 b{half_extent, height, -half_extent};
        const MathLib::Vec3 c{half_extent, height, half_extent};
        const MathLib::Vec3 d{-half_extent, height, half_extent};

        out.push_back(BvhLib::Triangle{.v0 = a, .material = material, .edge1 = b - a, .padding0 = 0u, .edge2 = c - a, .padding1 = 0u});
        out.push_back(BvhLib::Triangle{.v0 = a, .material = material, .edge1 = c - a, .padding0 = 0u, .edge2 = d - a, .padding1 = 0u});
    }

    //! Half-extent of the sounding patch, in metres. Small, and the size is load-bearing — see below.
    constexpr float PATCH_HALF_EXTENT{3.0f};

    /*!
        The analytic scene: a small sounding patch at y = 0 under a wide silent ceiling at y = 10.

        The ceiling is wide so that a ray leaving the ear upwards always meets it. **The patch is
        deliberately small, and getting this wrong makes the whole scene prove less than it looks
        like it proves.** With a patch as wide as the ceiling, a shallow *direct* ray reaches the
        floor at fifteen metres just as the ceiling echo does, so the two paths land in the same
        bins and finding energy there says nothing about whether reflection works at all.

        At three metres the arithmetic separates them cleanly, with the ear five metres up:

        - Direct, straight down: 5 m. Direct, to the patch corner: sqrt(25 + 18) = 6.56 m.
          Bins 14 to 19.
        - Reflected, straight up and back: 5 + 10 = 15 m. To the corner via the ceiling, which is
          the mirror image of the patch at y = 20: sqrt(225 + 18) = 15.59 m. Bins 43 to 45.

        Nothing direct can reach bin 43, and nothing reflected can reach bin 14.
    */
    [[nodiscard]] BvhLib::Bvh parallelPlateScene()
    {
        std::vector<BvhLib::Triangle> triangles;
        appendHorizontalQuad(triangles, 0.0f, PATCH_HALF_EXTENT, MATERIAL_NEON_PRIMARY); // Sings.
        appendHorizontalQuad(triangles, 10.0f, 200.0f, MATERIAL_PILLAR); // Silent, and reflects.
        return BvhLib::build(std::move(triangles));
    }

    //! The ear sits midway between the two plates, so both are exactly five metres away.
    constexpr MathLib::Vec3 EAR{0.0f, 5.0f, 0.0f};

    //! A flat unit spectrum and no air absorption, so that only geometry is under test.
    [[nodiscard]] Acoustics::GatherConfig plainConfig()
    {
        return Acoustics::GatherConfig{.hum_spectrum = {{1.0f, 1.0f, 1.0f, 1.0f}},
            .air_absorption_db_per_km = {{0.0f, 0.0f, 0.0f, 0.0f}},
            .direction_count = 2048u,
            .max_order = Acoustics::MAX_ORDER,
            .range_metres = 60.0f};
    }

    //! Returns the bin a path of the given length lands in.
    [[nodiscard]] uint32_t binOf(float path_metres)
    {
        return static_cast<uint32_t>(path_metres / (Acoustics::SPEED_OF_SOUND * Acoustics::BIN_SECONDS));
    }

    //! Returns the index of the earliest bin holding any energy, or BIN_COUNT if the response is silent.
    [[nodiscard]] uint32_t firstOccupiedBin(const Acoustics::ImpulseResponse& response);

    //! Returns the energy summed across every band of one bin.
    [[nodiscard]] float energyInBin(const Acoustics::ImpulseResponse& response, uint32_t bin)
    {
        float sum{0.0f};
        for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
            sum += response.at(band, bin);
        }
        return sum;
    }

    uint32_t firstOccupiedBin(const Acoustics::ImpulseResponse& response)
    {
        for (uint32_t bin{0u}; bin < Acoustics::BIN_COUNT; ++bin) {
            if (energyInBin(response, bin) > 0.0f) {
                return bin;
            }
        }
        return Acoustics::BIN_COUNT;
    }

}

TEST_CASE(the_gather_is_bit_identical_between_runs)
{
    const BvhLib::Bvh bvh{parallelPlateScene()};
    const std::vector<float> strengths{Acoustics::makeAcousticSourceStrengths()};

    const Acoustics::ImpulseResponse first{Acoustics::gather(bvh, strengths, EAR, plainConfig())};
    const Acoustics::ImpulseResponse second{Acoustics::gather(bvh, strengths, EAR, plainConfig())};

    /*
        Exact equality, not a tolerance. PROGRAM_INTERFACE.md publishes bit-identical replay as a
        guarantee, and a gather that differed in the last bit between two runs of the same binary
        would already have broken it — there is no random number generator anywhere in this
        repository and the direction set is a closed form, so there is nothing left to differ.
    */
    for (uint32_t index{0u}; index < first.bins.size(); ++index) {
        TEST_CHECK(first.bins[index] == second.bins[index]);
    }
}

TEST_CASE(fibonacci_directions_are_unit_length_and_distinct)
{
    constexpr uint32_t COUNT{512u};

    for (uint32_t index{0u}; index < COUNT; ++index) {
        const MathLib::Vec3 direction{Acoustics::fibonacciDirection(index, COUNT)};
        const float length_squared{direction.dot(direction)};
        TEST_CHECK(std::fabs(length_squared - 1.0f) < 1e-4f);
    }

    // Neighbouring directions must actually differ, or the set has collapsed and the coverage
    // claim is empty. The golden angle is what guarantees this.
    for (uint32_t index{1u}; index < COUNT; ++index) {
        const MathLib::Vec3 previous{Acoustics::fibonacciDirection(index - 1u, COUNT)};
        const MathLib::Vec3 current{Acoustics::fibonacciDirection(index, COUNT)};
        TEST_CHECK((current - previous).dot(current - previous) > 1e-6f);
    }

    // The set must cover both hemispheres, which is the property a gather depends on: an ear that
    // only heard upwards would miss the floor entirely.
    TEST_CHECK(Acoustics::fibonacciDirection(0u, COUNT).y > 0.9f);
    TEST_CHECK(Acoustics::fibonacciDirection(COUNT - 1u, COUNT).y < -0.9f);
}

TEST_CASE(arrivals_land_in_the_bins_arithmetic_predicts)
{
    const BvhLib::Bvh bvh{parallelPlateScene()};
    const std::vector<float> strengths{Acoustics::makeAcousticSourceStrengths()};
    const Acoustics::ImpulseResponse response{Acoustics::gather(bvh, strengths, EAR, plainConfig())};

    /*
        Two arrivals are exactly calculable, and if either is in the wrong bin the delay
        accumulation is wrong:

        - Straight down: 5 m to the sounding floor.
        - Straight up: 5 m to the silent ceiling, reflected, then 10 m back down to the floor —
          15 m in total.
    */
    const uint32_t direct_bin{binOf(5.0f)};
    const uint32_t reflected_bin{binOf(15.0f)};

    TEST_CHECK(direct_bin == 14u); // 5 / 0.343 = 14.6
    TEST_CHECK(reflected_bin == 43u); // 15 / 0.343 = 43.7

    TEST_CHECK(energyInBin(response, direct_bin) > 0.0f);
    TEST_CHECK(energyInBin(response, reflected_bin) > 0.0f);

    // The patch is small enough that no direct ray can reach the echo's bin, so energy there is
    // proof the reflection happened rather than a coincidence of a wide floor.
    for (uint32_t bin{binOf(7.0f)}; bin < binOf(14.0f); ++bin) {
        TEST_CHECK(energyInBin(response, bin) == 0.0f);
    }

    /*
        Nothing may arrive sooner than the perpendicular distance to the nearest sounding surface.
        This is the assertion that actually catches an off-by-one in the bin index, because it
        holds for every direction at once rather than for the one that happens to point straight
        down.
    */
    for (uint32_t bin{0u}; bin < direct_bin; ++bin) {
        TEST_CHECK(energyInBin(response, bin) == 0.0f);
    }
}

TEST_CASE(reflections_are_lossless_so_more_orders_only_ever_add)
{
    const BvhLib::Bvh bvh{parallelPlateScene()};
    const std::vector<float> strengths{Acoustics::makeAcousticSourceStrengths()};

    /*
        Every surface on the Grid is a perfect acoustic mirror, and this is what that means
        observably. Raising the reflection order cap can only add arrivals — it cannot change or
        diminish the ones already found, because nothing is subtracted on contact. If a later order
        ever moved an earlier bin, energy would be leaking backwards through the model.
    */
    Acoustics::GatherConfig direct_only{plainConfig()};
    direct_only.max_order = 0u;

    Acoustics::GatherConfig bounced{plainConfig()};
    bounced.max_order = 4u;

    const Acoustics::ImpulseResponse first{Acoustics::gather(bvh, strengths, EAR, direct_only)};
    const Acoustics::ImpulseResponse full{Acoustics::gather(bvh, strengths, EAR, bounced)};

    TEST_CHECK(full.total() > first.total());

    for (uint32_t index{0u}; index < first.bins.size(); ++index) {
        TEST_CHECK(full.bins[index] >= first.bins[index]);
    }

    // The direct arrival is unchanged by anything that happens later, bit for bit.
    TEST_CHECK(full.at(0u, binOf(5.0f)) == first.at(0u, binOf(5.0f)));

    // With no reflections at all the ceiling echo cannot exist; with four it must. The patch is
    // sized so that nothing direct can land in that bin either way.
    TEST_CHECK(energyInBin(first, binOf(15.0f)) == 0.0f);
    TEST_CHECK(energyInBin(full, binOf(15.0f)) > 0.0f);
}

TEST_CASE(a_single_arrival_never_exceeds_the_source_strength)
{
    // One direction, straight down onto the sounding floor from one metre up. At the one-metre
    // reference spreading is exactly unity, which is the loudest a unit source may ever be heard:
    // anything above this means the 1/r squared floor has been applied the wrong way round.
    std::vector<BvhLib::Triangle> triangles;
    appendHorizontalQuad(triangles, 0.0f, 200.0f, MATERIAL_NEON_PRIMARY);
    const BvhLib::Bvh bvh{BvhLib::build(std::move(triangles))};

    const std::vector<float> strengths{Acoustics::makeAcousticSourceStrengths()};

    Acoustics::GatherConfig config{plainConfig()};
    config.direction_count = 1024u;

    const Acoustics::ImpulseResponse response{Acoustics::gather(bvh, strengths, MathLib::Vec3{0.0f, 1.0f, 0.0f}, config)};

    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
        for (uint32_t bin{0u}; bin < Acoustics::BIN_COUNT; ++bin) {
            // Many rays may land in one bin, so the bound is per ray times the number of rays that
            // can reach it — but no bin may exceed the whole ray budget at unit strength.
            TEST_CHECK(response.at(band, bin) <= static_cast<float>(config.direction_count));
        }
    }

    // Nothing arrives before the perpendicular metre.
    for (uint32_t bin{0u}; bin < binOf(1.0f); ++bin) {
        TEST_CHECK(energyInBin(response, bin) == 0.0f);
    }
}

TEST_CASE(air_absorption_is_applied_and_grows_with_distance)
{
    /*
        The values are authored per listener rather than computed, so what is left to check is that
        the gather actually applies them and applies them per band. This is the term that gives the
        Grid a physical acoustic horizon — without it the range cap would be a budget decision
        rather than a consequence of the air.
    */
    const BvhLib::Bvh bvh{parallelPlateScene()};
    const std::vector<float> strengths{Acoustics::makeAcousticSourceStrengths()};

    Acoustics::GatherConfig config{plainConfig()};
    config.air_absorption_db_per_km = {{0.0f, 100.0f, 1000.0f, 10000.0f}};

    const Acoustics::ImpulseResponse response{Acoustics::gather(bvh, strengths, EAR, config)};

    // Band 0 has transparent air; the rest are progressively more opaque, so they must be
    // progressively quieter in every bin that holds anything at all.
    for (uint32_t bin{0u}; bin < Acoustics::BIN_COUNT; ++bin) {
        if (response.at(0u, bin) <= 0.0f) {
            continue;
        }
        TEST_CHECK(response.at(1u, bin) < response.at(0u, bin));
        TEST_CHECK(response.at(2u, bin) < response.at(1u, bin));
        TEST_CHECK(response.at(3u, bin) < response.at(2u, bin));
    }

    // And the loss must grow with path length: the late echo is hit harder than the direct arrival.
    const float direct_ratio{response.at(1u, binOf(5.0f)) / response.at(0u, binOf(5.0f))};
    const float echo_ratio{response.at(1u, binOf(15.0f)) / response.at(0u, binOf(15.0f))};
    TEST_CHECK(echo_ratio < direct_ratio);
}

TEST_CASE(an_empty_grid_is_silent)
{
    const BvhLib::Bvh empty{};
    const std::vector<float> strengths{Acoustics::makeAcousticSourceStrengths()};
    const Acoustics::ImpulseResponse response{Acoustics::gather(empty, strengths, EAR, plainConfig())};

    TEST_CHECK(response.total() == 0.0f);
}

TEST_CASE(the_response_is_a_pure_function_of_the_grid_and_the_ear)
{
    /*
        This is what licenses solving on demand rather than every tick, and it is a stronger licence
        than the renderer has. A frame has to be compared against the state it was drawn from,
        because floating-point camera integration can wander; a gather cannot wander at all. Same
        Grid, same materials, same ear, same config gives bit-identical output — so a solve whose
        inputs are unchanged may be skipped outright, and the skipped answer is not an approximation
        of the real one, it *is* the real one.

        The test therefore has two halves, and the second is the one that matters: every input must
        be able to change the output, or it does not belong in the cache key and a cache built on it
        would return a stale answer for a listener that had moved.
    */
    const BvhLib::Bvh bvh{parallelPlateScene()};
    const std::vector<float> strengths{Acoustics::makeAcousticSourceStrengths()};

    const Acoustics::ImpulseResponse here{Acoustics::gather(bvh, strengths, EAR, plainConfig())};
    const Acoustics::ImpulseResponse here_again{Acoustics::gather(bvh, strengths, EAR, plainConfig())};

    TEST_CHECK(here.bins == here_again.bins);

    // Moving the ear must move the answer, or ear position does not belong in the key.
    const Acoustics::ImpulseResponse lower{Acoustics::gather(bvh, strengths, MathLib::Vec3{0.0f, 2.0f, 0.0f}, plainConfig())};
    TEST_CHECK(!(lower.bins == here.bins));

    // An ear closer to the sounding floor hears it sooner: the first occupied bin is the
    // perpendicular drop, and two metres is a shorter drop than five.
    TEST_CHECK(firstOccupiedBin(lower) == binOf(2.0f));
    TEST_CHECK(firstOccupiedBin(here) == binOf(5.0f));
    TEST_CHECK(firstOccupiedBin(lower) < firstOccupiedBin(here));

    // The config must be in the key too: a different ray budget is a different answer.
    Acoustics::GatherConfig sparse{plainConfig()};
    sparse.direction_count = 256u;
    const Acoustics::ImpulseResponse coarse{Acoustics::gather(bvh, strengths, EAR, sparse)};
    TEST_CHECK(!(coarse.bins == here.bins));
}

TEST_CASE(a_material_index_past_the_table_is_silent_and_still_reflects)
{
    /*
        The table is indexed by a triangle's material with no guarantee from the type system that the
        two agree, so a short table is a caller error, and reading it unchecked would be undefined
        behaviour — a read past the end of a vector, which crashes somewhere else entirely or, worse,
        does not.

        An unknown surface is treated as silent and still reflecting, which is the only sane reading
        of a surface nobody described, and both halves are checked here: the sounding patch goes
        quiet when its strength is unreachable, and the ceiling it never described still produces
        its echo of the one material that remains.
    */
    const BvhLib::Bvh bvh{parallelPlateScene()};

    // Long enough to describe the floor, too short to describe the pillar the ceiling is made of.
    std::vector<float> truncated(MATERIAL_NEON_PRIMARY + 1u, 0.0f);
    truncated[MATERIAL_NEON_PRIMARY] = 1.0f;

    const Acoustics::ImpulseResponse response{Acoustics::gather(bvh, truncated, EAR, plainConfig())};

    // The floor is described and sings; the direct arrival is unaffected by the missing entry.
    TEST_CHECK(energyInBin(response, binOf(5.0f)) > 0.0f);

    // The ceiling is undescribed, so it is silent — but it must still reflect, or the echo of the
    // floor off it would vanish too.
    TEST_CHECK(energyInBin(response, binOf(15.0f)) > 0.0f);

    // An empty table describes nothing at all, so nothing sings anywhere.
    const std::vector<float> nothing(1u, 0.0f);
    const Acoustics::ImpulseResponse silent{Acoustics::gather(bvh, nothing, EAR, plainConfig())};
    TEST_CHECK(silent.total() == 0.0f);
}

// ---------------------------------------------------------------------------------------------
// The call delivery: the point-source half of hearing, held to its own arithmetic
// ---------------------------------------------------------------------------------------------

namespace
{

    //! Wraps loose triangles as a one-instance scene at the identity — or an empty scene, which is
    //! a legitimate Grid whose air still carries a call.
    [[nodiscard]] BvhLib::Scene sceneAround(std::vector<BvhLib::Triangle> triangles)
    {
        BvhLib::Scene scene{};
        if (!triangles.empty()) {
            BvhLib::Bvh hierarchy{BvhLib::build(std::move(triangles))};
            scene.instances.push_back(BvhLib::makeInstance(hierarchy, 0u, MathLib::Mat4::identity()));
            scene.geometries.push_back(std::move(hierarchy));
        }
        return scene;
    }

    //! Appends a vertical axis-aligned quad in the plane x = `x`, as two triangles.
    void appendVerticalQuadX(std::vector<BvhLib::Triangle>& out, float x, float y_min, float y_max, float z_min, float z_max, uint32_t material)
    {
        const MathLib::Vec3 a{x, y_min, z_min};
        const MathLib::Vec3 b{x, y_max, z_min};
        const MathLib::Vec3 c{x, y_max, z_max};
        const MathLib::Vec3 d{x, y_min, z_max};

        out.push_back(BvhLib::Triangle{.v0 = a, .material = material, .edge1 = b - a, .padding0 = 0u, .edge2 = c - a, .padding1 = 0u});
        out.push_back(BvhLib::Triangle{.v0 = a, .material = material, .edge1 = c - a, .padding0 = 0u, .edge2 = d - a, .padding1 = 0u});
    }

    /*
        The call geometry every delivery test shares, chosen so each expected bin sits well clear
        of a boundary:

        - Source at (0, 1, 0), ear at (3.6, 1, 0). Direct path 3.6 m, bin 10 (3.6 / 0.343 = 10.5).
        - Floor image below y = 0: path sqrt(3.6² + 2²) = 4.118 m, bin 12 (12.007).
        - Wall at x = 5 heard from (0.2, 1, 0): out and back 9.8 m, bin 28 (28.57).
    */
    constexpr MathLib::Vec3 CALL_SOURCE{0.0f, 1.0f, 0.0f};
    constexpr MathLib::Vec3 CALL_EAR{3.6f, 1.0f, 0.0f};

    //! Energy of the unit-strength direct arrival: spreading alone, over 3.6 metres.
    constexpr float DIRECT_ENERGY{1.0f / (3.6f * 3.6f)};

    //! A flat unit spectrum and transparent air, so only geometry is under test.
    [[nodiscard]] Acoustics::CallConfig plainCallConfig()
    {
        return Acoustics::CallConfig{};
    }

} // namespace

TEST_CASE(a_call_is_heard_directly_at_the_distance_the_air_dictates)
{
    // A wide silent floor beneath: real geometry near the path, none of it in the way.
    std::vector<BvhLib::Triangle> triangles;
    appendHorizontalQuad(triangles, 0.0f, 200.0f, MATERIAL_FLOOR);
    const BvhLib::Scene scene{sceneAround(std::move(triangles))};

    const Acoustics::ImpulseResponse response{Acoustics::deliverCall(scene, Acoustics::Reflectors{}, CALL_SOURCE, 1.0f, CALL_EAR, plainCallConfig())};

    const uint32_t direct_bin{binOf(3.6f)};
    TEST_CHECK(direct_bin == 10u); // 3.6 / 0.343 = 10.5

    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
        TEST_CHECK_CLOSE(response.at(band, direct_bin), DIRECT_ENERGY, 1e-7f);
    }

    // One arrival and nothing else: no reflectors were declared, so no image may appear.
    for (uint32_t bin{0u}; bin < Acoustics::BIN_COUNT; ++bin) {
        if (bin != direct_bin) {
            TEST_CHECK(energyInBin(response, bin) == 0.0f);
        }
    }
}

TEST_CASE(a_call_within_arms_reach_arrives_at_full_strength_in_bin_zero)
{
    /*
        The spreading floor, from the caller's side: closer than the one-metre reference nothing is
        amplified, so a creature's own call reaches its own ears at exactly the strength it called
        with. The scene is empty on purpose — the direct path needs air, not triangles.
    */
    const BvhLib::Scene empty{sceneAround({})};
    const MathLib::Vec3 own_ear{0.2f, 1.0f, 0.0f};

    const Acoustics::ImpulseResponse response{Acoustics::deliverCall(empty, Acoustics::Reflectors{}, CALL_SOURCE, 0.75f, own_ear, plainCallConfig())};

    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
        TEST_CHECK(response.at(band, 0u) == 0.75f);
    }
    for (uint32_t bin{1u}; bin < Acoustics::BIN_COUNT; ++bin) {
        TEST_CHECK(energyInBin(response, bin) == 0.0f);
    }
}

TEST_CASE(an_echo_returns_from_the_level_below_and_adds_to_the_direct_path)
{
    // The analytic image-source case: one flat level, one call, two arrivals — both computable by
    // hand, in separate bins, with nothing else anywhere.
    std::vector<BvhLib::Triangle> triangles;
    appendHorizontalQuad(triangles, 0.0f, 200.0f, MATERIAL_FLOOR);
    const BvhLib::Scene scene{sceneAround(std::move(triangles))};

    const Acoustics::Reflectors reflectors{.level_heights = {0.0f}, .faces = {}};
    const Acoustics::ImpulseResponse response{Acoustics::deliverCall(scene, reflectors, CALL_SOURCE, 1.0f, CALL_EAR, plainCallConfig())};

    const float echo_path{std::sqrt((3.6f * 3.6f) + 4.0f)}; // The image at y = -1, seen from y = +1.
    const uint32_t direct_bin{binOf(3.6f)};
    const uint32_t echo_bin{binOf(echo_path)};
    TEST_CHECK(echo_bin == 12u); // 4.118 / 0.343 = 12.007

    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
        TEST_CHECK_CLOSE(response.at(band, direct_bin), DIRECT_ENERGY, 1e-7f);
        TEST_CHECK_CLOSE(response.at(band, echo_bin), 1.0f / (echo_path * echo_path), 1e-7f);
    }

    for (uint32_t bin{0u}; bin < Acoustics::BIN_COUNT; ++bin) {
        if ((bin != direct_bin) && (bin != echo_bin)) {
            TEST_CHECK(energyInBin(response, bin) == 0.0f);
        }
    }
}

TEST_CASE(a_wall_face_answers_a_call_with_the_echo_a_creature_could_range)
{
    /*
        Echolocation in one scene: an emitter and an ear a fifth of a metre apart, a vertical wall
        five metres ahead, and the out-and-back path of 9.8 metres arriving in the bin the speed of
        sound dictates. This is exactly what the terraced floor's tilted risers cannot do — a
        22.6 degree facet deflects the call skyward — and exactly what any genuinely vertical face
        on the Grid does for free.
    */
    std::vector<BvhLib::Triangle> triangles;
    appendVerticalQuadX(triangles, 5.0f, 0.0f, 4.0f, -3.0f, 3.0f, MATERIAL_PILLAR);
    const BvhLib::Scene scene{sceneAround(std::move(triangles))};

    // The wall as a reflector: origin at its low corner, edges wound so the normal faces -X, back
    // towards the caller.
    const Acoustics::RectFace wall{.origin = MathLib::Vec3{5.0f, 0.0f, -3.0f}, .edge_u = MathLib::Vec3{0.0f, 0.0f, 6.0f}, .edge_v = MathLib::Vec3{0.0f, 4.0f, 0.0f}};
    const Acoustics::Reflectors reflectors{.level_heights = {}, .faces = {wall}};

    const MathLib::Vec3 ear{0.2f, 1.0f, 0.0f};
    const Acoustics::ImpulseResponse response{Acoustics::deliverCall(scene, reflectors, CALL_SOURCE, 1.0f, ear, plainCallConfig())};

    const uint32_t echo_bin{binOf(9.8f)};
    TEST_CHECK(echo_bin == 28u); // 9.8 / 0.343 = 28.6

    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
        TEST_CHECK(response.at(band, 0u) == 1.0f); // Its own call, at the spreading floor.
        TEST_CHECK_CLOSE(response.at(band, echo_bin), 1.0f / (9.8f * 9.8f), 1e-7f);
    }

    for (uint32_t bin{1u}; bin < Acoustics::BIN_COUNT; ++bin) {
        if (bin != echo_bin) {
            TEST_CHECK(energyInBin(response, bin) == 0.0f);
        }
    }
}

TEST_CASE(a_candidate_level_with_no_geometry_beneath_it_never_arrives)
{
    // The same call over an empty world: the enumeration proposes the level, and the validation
    // ray — finding nothing at the reflection point — is the plausibility test that disposes.
    const BvhLib::Scene empty{sceneAround({})};
    const Acoustics::Reflectors reflectors{.level_heights = {0.0f}, .faces = {}};

    const Acoustics::ImpulseResponse response{Acoustics::deliverCall(empty, reflectors, CALL_SOURCE, 1.0f, CALL_EAR, plainCallConfig())};

    TEST_CHECK(energyInBin(response, binOf(3.6f)) > 0.0f); // The air still carries the direct path.
    TEST_CHECK(energyInBin(response, 12u) == 0.0f); // The echo's bin stays empty: nothing stood there.
}

TEST_CASE(a_wall_standing_where_the_floor_should_answer_does_not_answer_for_it)
{
    /*
        The alignment guard: real geometry stands exactly at the reflection point, and the path to
        it is clear, but the surface is a vertical wall where the mirror plane is horizontal. A
        hit is not a mirror unless it lies in the mirror's own plane — without this check a wall
        standing on a terrace would return echoes computed with the terrace's arithmetic.
    */
    std::vector<BvhLib::Triangle> triangles;
    appendVerticalQuadX(triangles, 1.8f, -1.0f, 0.6f, -1.0f, 1.0f, MATERIAL_PILLAR); // Through (1.8, 0, 0).
    const BvhLib::Scene scene{sceneAround(std::move(triangles))};

    const Acoustics::Reflectors reflectors{.level_heights = {0.0f}, .faces = {}};
    const Acoustics::ImpulseResponse response{Acoustics::deliverCall(scene, reflectors, CALL_SOURCE, 1.0f, CALL_EAR, plainCallConfig())};

    // The direct path passes above the wall's top edge; the level's candidate strikes the wall at
    // the reflection point and is refused for its orientation.
    TEST_CHECK(energyInBin(response, binOf(3.6f)) > 0.0f);
    TEST_CHECK(energyInBin(response, 12u) == 0.0f);
}

TEST_CASE(an_occluded_direct_path_dims_to_nothing_while_the_floor_still_answers)
{
    /*
        The bistatic property in one scene: a wall stands between caller and listener, high enough
        to blot out the whole occlusion probe, while the bounce off the floor passes under it. A
        creature behind a barrier is quiet but not gone — its echo arrives without its voice.
    */
    std::vector<BvhLib::Triangle> triangles;
    appendHorizontalQuad(triangles, 0.0f, 200.0f, MATERIAL_FLOOR);
    appendVerticalQuadX(triangles, 1.8f, 0.5f, 4.0f, -3.0f, 3.0f, MATERIAL_GLASS);
    const BvhLib::Scene scene{sceneAround(std::move(triangles))};

    const Acoustics::Reflectors reflectors{.level_heights = {0.0f}, .faces = {}};
    const Acoustics::ImpulseResponse response{Acoustics::deliverCall(scene, reflectors, CALL_SOURCE, 1.0f, CALL_EAR, plainCallConfig())};

    TEST_CHECK(energyInBin(response, binOf(3.6f)) == 0.0f); // Every probe sample is blocked.
    TEST_CHECK(energyInBin(response, 12u) > 0.0f); // The floor image ducks under the wall.
}

TEST_CASE(a_grazing_occluder_dims_the_call_by_the_fraction_of_the_probe_it_blocks)
{
    /*
        The graded shadow, at its exact value. A curtain wall hangs with its bottom edge at the
        source's own height, so of the probe's six sphere samples the three above centre are
        blocked and the three below pass under: the fraction is one half, not one and not zero.
        This is the difference between a thin post dimming a call and a thin post silencing it —
        and it is occlusion sampling, never to be described as diffraction.
    */
    std::vector<BvhLib::Triangle> triangles;
    appendVerticalQuadX(triangles, 1.8f, 1.0f, 4.0f, -3.0f, 3.0f, MATERIAL_GLASS);
    const BvhLib::Scene scene{sceneAround(std::move(triangles))};

    const Acoustics::ImpulseResponse response{Acoustics::deliverCall(scene, Acoustics::Reflectors{}, CALL_SOURCE, 1.0f, CALL_EAR, plainCallConfig())};

    // The half is exact — three samples of six — and the tolerance only covers the square root in
    // the path arithmetic.
    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
        TEST_CHECK_CLOSE(response.at(band, binOf(3.6f)), 0.5f * DIRECT_ENERGY, 1e-7f);
    }
}

TEST_CASE(a_call_beyond_the_acoustic_horizon_is_never_heard)
{
    const BvhLib::Scene empty{sceneAround({})};
    const MathLib::Vec3 far_ear{25.0f, 1.0f, 0.0f}; // Past the 20 m total-path cap.

    const Acoustics::ImpulseResponse response{Acoustics::deliverCall(empty, Acoustics::Reflectors{}, CALL_SOURCE, 1.0f, far_ear, plainCallConfig())};
    TEST_CHECK(response.total() == 0.0f);
}

TEST_CASE(a_silent_or_meaningless_call_is_no_call)
{
    const BvhLib::Scene empty{sceneAround({})};

    TEST_CHECK(Acoustics::deliverCall(empty, Acoustics::Reflectors{}, CALL_SOURCE, 0.0f, CALL_EAR, plainCallConfig()).total() == 0.0f);
    TEST_CHECK(Acoustics::deliverCall(empty, Acoustics::Reflectors{}, CALL_SOURCE, -5.0f, CALL_EAR, plainCallConfig()).total() == 0.0f);
}

TEST_CASE(air_absorption_thins_a_call_by_band_and_by_distance)
{
    const BvhLib::Scene empty{sceneAround({})};

    Acoustics::CallConfig config{plainCallConfig()};
    config.air_absorption_db_per_km = {{0.0f, 100.0f, 1000.0f, 10000.0f}};

    const Acoustics::ImpulseResponse near_response{Acoustics::deliverCall(empty, Acoustics::Reflectors{}, CALL_SOURCE, 1.0f, CALL_EAR, config)};
    const MathLib::Vec3 far_ear{10.0f, 1.0f, 0.0f};
    const Acoustics::ImpulseResponse far_response{Acoustics::deliverCall(empty, Acoustics::Reflectors{}, CALL_SOURCE, 1.0f, far_ear, config)};

    const uint32_t near_bin{binOf(3.6f)};
    const uint32_t far_bin{binOf(10.0f)};

    // Opaque bands are quieter than transparent ones at the same distance.
    TEST_CHECK(near_response.at(1u, near_bin) < near_response.at(0u, near_bin));
    TEST_CHECK(near_response.at(2u, near_bin) < near_response.at(1u, near_bin));
    TEST_CHECK(near_response.at(3u, near_bin) < near_response.at(2u, near_bin));

    // And the loss grows with the metres crossed: the far ear loses a larger fraction.
    const float near_ratio{near_response.at(1u, near_bin) / near_response.at(0u, near_bin)};
    const float far_ratio{far_response.at(1u, far_bin) / far_response.at(0u, far_bin)};
    TEST_CHECK(far_ratio < near_ratio);
}

TEST_CASE(a_delivery_is_bit_identical_between_runs)
{
    std::vector<BvhLib::Triangle> triangles;
    appendHorizontalQuad(triangles, 0.0f, 200.0f, MATERIAL_FLOOR);
    appendVerticalQuadX(triangles, 5.0f, 0.0f, 4.0f, -3.0f, 3.0f, MATERIAL_PILLAR);
    const BvhLib::Scene scene{sceneAround(std::move(triangles))};

    const Acoustics::RectFace wall{.origin = MathLib::Vec3{5.0f, 0.0f, -3.0f}, .edge_u = MathLib::Vec3{0.0f, 0.0f, 6.0f}, .edge_v = MathLib::Vec3{0.0f, 4.0f, 0.0f}};
    const Acoustics::Reflectors reflectors{.level_heights = {0.0f}, .faces = {wall}};

    const Acoustics::ImpulseResponse first{Acoustics::deliverCall(scene, reflectors, CALL_SOURCE, 1.0f, CALL_EAR, plainCallConfig())};
    const Acoustics::ImpulseResponse second{Acoustics::deliverCall(scene, reflectors, CALL_SOURCE, 1.0f, CALL_EAR, plainCallConfig())};

    TEST_CHECK(first.bins == second.bins);
}

TEST_CASE(an_ear_sealed_in_anothers_hull_is_deaf_until_the_hull_is_its_own)
{
    /*
        A closed tetrahedron around the ear, over a singing floor. Sealed in somebody else's hull
        the ear is deaf — every gather ray dies inside a silent shell — and that is honest
        occlusion. Sealed in its *own* hull it must hear the world anyway, because ears sit on
        bodies, and a body that deafened its own ears would end hearing the moment bodies became
        real. The skip is the difference, and this is the test that keeps it one.
    */
    std::vector<BvhLib::Triangle> floor_triangles;
    appendHorizontalQuad(floor_triangles, 0.0f, 200.0f, MATERIAL_NEON_PRIMARY);
    BvhLib::Bvh floor_bvh{BvhLib::build(std::move(floor_triangles))};

    // Alternate corners of a cube: a tetrahedron with the origin strictly inside.
    const std::array<MathLib::Vec3, 4u> corners{
        MathLib::Vec3{0.15f, 0.15f, 0.15f}, MathLib::Vec3{0.15f, -0.15f, -0.15f}, MathLib::Vec3{-0.15f, 0.15f, -0.15f}, MathLib::Vec3{-0.15f, -0.15f, 0.15f}};
    const std::array<std::array<uint32_t, 3u>, 4u> faces{{{0u, 1u, 2u}, {0u, 3u, 1u}, {0u, 2u, 3u}, {1u, 3u, 2u}}};

    std::vector<BvhLib::Triangle> hull_triangles;
    for (const std::array<uint32_t, 3u>& face : faces) {
        const MathLib::Vec3& a{corners[face[0]]};
        const MathLib::Vec3& b{corners[face[1]]};
        const MathLib::Vec3& c{corners[face[2]]};
        hull_triangles.push_back(BvhLib::Triangle{.v0 = a, .material = MATERIAL_FLOOR, .edge1 = b - a, .padding0 = 0u, .edge2 = c - a, .padding1 = 0u});
    }
    BvhLib::Bvh hull_bvh{BvhLib::build(std::move(hull_triangles))};

    const MathLib::Vec3 ear{0.0f, 1.0f, 0.0f};

    BvhLib::Scene scene{};
    scene.instances.push_back(BvhLib::makeInstance(floor_bvh, 0u, MathLib::Mat4::identity()));
    scene.instances.push_back(BvhLib::makeInstance(hull_bvh, 1u, MathLib::Mat4::translate(ear)));
    scene.geometries.push_back(std::move(floor_bvh));
    scene.geometries.push_back(std::move(hull_bvh));

    const std::vector<float> strengths{Acoustics::makeAcousticSourceStrengths()};

    Acoustics::GatherConfig sealed{plainConfig()};
    const Acoustics::ImpulseResponse deaf{Acoustics::gather(scene, strengths, ear, sealed)};

    Acoustics::GatherConfig own_hull{plainConfig()};
    own_hull.skip_instance = 1u;
    const Acoustics::ImpulseResponse hearing{Acoustics::gather(scene, strengths, ear, own_hull)};
    TEST_CHECK(hearing.total() > 0.0f);

    /*
        A hundredfold quieter rather than silent, because a sealed hull is sealed only up to the
        surface epsilon: a reflected ray nudged off a hit within a millimetre of a corner can
        restart on the far side of the adjacent face and escape. A handful of the two thousand
        directions do, and that is the reflection machinery being honest about its epsilon rather
        than the seal failing. The ordering is the claim; the skip is the difference.
    */
    TEST_CHECK(deaf.total() < (hearing.total() / 100.0f));
}

TEST_CASE(a_call_from_inside_a_hull_is_gagged_unless_the_hull_is_the_callers)
{
    // The same tetrahedron, now around the caller. A voice inside somebody else's hull is gagged,
    // which is honest; a voice inside its own body must carry, because that is where voices live.
    const std::array<MathLib::Vec3, 4u> corners{
        MathLib::Vec3{0.15f, 0.15f, 0.15f}, MathLib::Vec3{0.15f, -0.15f, -0.15f}, MathLib::Vec3{-0.15f, 0.15f, -0.15f}, MathLib::Vec3{-0.15f, -0.15f, 0.15f}};
    const std::array<std::array<uint32_t, 3u>, 4u> faces{{{0u, 1u, 2u}, {0u, 3u, 1u}, {0u, 2u, 3u}, {1u, 3u, 2u}}};

    std::vector<BvhLib::Triangle> hull_triangles;
    for (const std::array<uint32_t, 3u>& face : faces) {
        const MathLib::Vec3& a{corners[face[0]]};
        const MathLib::Vec3& b{corners[face[1]]};
        const MathLib::Vec3& c{corners[face[2]]};
        hull_triangles.push_back(BvhLib::Triangle{.v0 = a, .material = MATERIAL_PILLAR, .edge1 = b - a, .padding0 = 0u, .edge2 = c - a, .padding1 = 0u});
    }
    BvhLib::Bvh hull_bvh{BvhLib::build(std::move(hull_triangles))};

    BvhLib::Scene scene{};
    scene.instances.push_back(BvhLib::makeInstance(hull_bvh, 0u, MathLib::Mat4::translate(CALL_SOURCE)));
    scene.geometries.push_back(std::move(hull_bvh));

    const Acoustics::ImpulseResponse gagged{Acoustics::deliverCall(scene, Acoustics::Reflectors{}, CALL_SOURCE, 1.0f, CALL_EAR, plainCallConfig())};
    TEST_CHECK(gagged.total() == 0.0f);

    Acoustics::CallConfig own_body{plainCallConfig()};
    own_body.caller_instance = 0u;
    const Acoustics::ImpulseResponse carried{Acoustics::deliverCall(scene, Acoustics::Reflectors{}, CALL_SOURCE, 1.0f, CALL_EAR, own_body)};
    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
        TEST_CHECK_CLOSE(carried.at(band, binOf(3.6f)), DIRECT_ENERGY, 1e-7f);
    }
}

TEST_CASE(outward_box_faces_face_outward_and_omit_the_bottom)
{
    const MathLib::Vec3 centre{1.0f, 2.0f, 3.0f};
    const MathLib::Vec3 half_extents{0.5f, 1.0f, 2.0f};
    const std::array<Acoustics::RectFace, 5u> faces{Acoustics::outwardBoxFaces(centre, half_extents)};

    uint32_t tops{0u};
    for (const Acoustics::RectFace& face : faces) {
        const MathLib::Vec3 cross{face.edge_u.cross(face.edge_v)};
        const MathLib::Vec3 normal{cross.normalised()};

        // The winding is the statement of which side reflects: every normal points away from the
        // centre, and none points down.
        const MathLib::Vec3 face_centre{face.origin + ((face.edge_u + face.edge_v) * 0.5f)};
        TEST_CHECK(normal.dot(face_centre - centre) > 0.0f);
        TEST_CHECK(normal.y > -0.5f);

        if (normal.y > 0.5f) {
            ++tops;
        }

        // Every corner of every face lies on the box's surface.
        for (const MathLib::Vec3& corner : {face.origin, face.origin + face.edge_u, face.origin + face.edge_v, face.origin + face.edge_u + face.edge_v}) {
            TEST_CHECK(std::fabs(corner.x - centre.x) <= half_extents.x + 1e-6f);
            TEST_CHECK(std::fabs(corner.y - centre.y) <= half_extents.y + 1e-6f);
            TEST_CHECK(std::fabs(corner.z - centre.z) <= half_extents.z + 1e-6f);
        }
    }

    TEST_CHECK(tops == 1u); // Exactly one top, no bottom.
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
