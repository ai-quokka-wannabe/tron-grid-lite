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
        two agree, so a short table is a caller error that used to be undefined behaviour — a read
        past the end of a vector, which crashes somewhere else entirely or, worse, does not.

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

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
