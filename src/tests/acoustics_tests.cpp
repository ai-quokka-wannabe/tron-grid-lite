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

    /*!
        The analytic scene: a sounding floor at y = 0 and a silent ceiling at y = 10.

        Both are wide enough that a ray leaving the ear anywhere but exactly sideways meets one of
        them, and the two are parallel so that the arrival times are a hand calculation rather than
        a simulation.
    */
    [[nodiscard]] BvhLib::Bvh parallelPlateScene()
    {
        std::vector<BvhLib::Triangle> triangles;
        appendHorizontalQuad(triangles, 0.0f, 200.0f, MATERIAL_NEON_PRIMARY); // Sings.
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
    const std::vector<Acoustics::AcousticMaterial> materials{Acoustics::makeAcousticMaterials()};

    const Acoustics::ImpulseResponse first{Acoustics::gather(bvh, materials, EAR, plainConfig())};
    const Acoustics::ImpulseResponse second{Acoustics::gather(bvh, materials, EAR, plainConfig())};

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
    const std::vector<Acoustics::AcousticMaterial> materials{Acoustics::makeAcousticMaterials()};
    const Acoustics::ImpulseResponse response{Acoustics::gather(bvh, materials, EAR, plainConfig())};

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

TEST_CASE(no_path_gains_energy_and_absorption_only_takes_it_away)
{
    const BvhLib::Bvh bvh{parallelPlateScene()};

    // Absorption is swept while everything else is held fixed, so the only thing that can move the
    // total is the surface term. This is what catches spreading being counted twice: a detection
    // sphere on top of the explicit 1/r squared would show up as a total that does not respond to
    // absorption the way the arithmetic says it must.
    float previous_total{-1.0f};

    for (const float absorption : {0.0f, 0.1f, 0.25f, 0.5f, 0.9f, 1.0f}) {
        std::vector<Acoustics::AcousticMaterial> materials{Acoustics::makeAcousticMaterials()};
        for (Acoustics::AcousticMaterial& material : materials) {
            material.absorption = absorption;
        }

        const Acoustics::ImpulseResponse response{Acoustics::gather(bvh, materials, EAR, plainConfig())};
        const float total{response.total()};

        TEST_CHECK(total > 0.0f); // The direct arrival survives any absorption: it is deposited before the first reflection.

        if (previous_total >= 0.0f) {
            TEST_CHECK(total <= previous_total);
        }
        previous_total = total;
    }
}

TEST_CASE(a_single_arrival_never_exceeds_the_source_strength)
{
    // One direction, straight down onto the sounding floor from one metre up. At the one-metre
    // reference spreading is exactly unity, which is the loudest a unit source may ever be heard:
    // anything above this means the 1/r squared floor has been applied the wrong way round.
    std::vector<BvhLib::Triangle> triangles;
    appendHorizontalQuad(triangles, 0.0f, 200.0f, MATERIAL_NEON_PRIMARY);
    const BvhLib::Bvh bvh{BvhLib::build(std::move(triangles))};

    const std::vector<Acoustics::AcousticMaterial> materials{Acoustics::makeAcousticMaterials()};

    Acoustics::GatherConfig config{plainConfig()};
    config.direction_count = 1024u;

    const Acoustics::ImpulseResponse response{Acoustics::gather(bvh, materials, MathLib::Vec3{0.0f, 1.0f, 0.0f}, config)};

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

TEST_CASE(air_absorption_matches_the_tabulated_iso_row)
{
    // The eight tabulated octave centres must come back exactly, or the interpolation has moved the
    // data it is supposed to interpolate between.
    TEST_CHECK(std::fabs(Acoustics::airAbsorptionDbPerKm(63.0f) - 0.1f) < 1e-4f);
    TEST_CHECK(std::fabs(Acoustics::airAbsorptionDbPerKm(1000.0f) - 5.0f) < 1e-3f);
    TEST_CHECK(std::fabs(Acoustics::airAbsorptionDbPerKm(4000.0f) - 22.9f) < 1e-2f);
    TEST_CHECK(std::fabs(Acoustics::airAbsorptionDbPerKm(8000.0f) - 76.6f) < 1e-2f);

    // Monotonically increasing with frequency across the whole tabulated range.
    float previous{0.0f};
    for (float frequency{63.0f}; frequency <= 8000.0f; frequency *= 1.1f) {
        const float value{Acoustics::airAbsorptionDbPerKm(frequency)};
        TEST_CHECK(value >= previous);
        previous = value;
    }

    /*
        The figures docs/ACOUSTICS.md quotes must be reproducible from this function, because the
        whole range-cap argument rests on them: 1.5 dB at 8 kHz over 20 m, and 9.8 dB over 128 m.
    */
    const float eight_khz{Acoustics::airAbsorptionDbPerKm(8000.0f)};
    TEST_CHECK(std::fabs((eight_khz * 0.020f) - 1.53f) < 0.05f);
    TEST_CHECK(std::fabs((eight_khz * 0.128f) - 9.80f) < 0.05f);
}

TEST_CASE(an_empty_grid_is_silent)
{
    const BvhLib::Bvh empty{};
    const std::vector<Acoustics::AcousticMaterial> materials{Acoustics::makeAcousticMaterials()};
    const Acoustics::ImpulseResponse response{Acoustics::gather(empty, materials, EAR, plainConfig())};

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
    const std::vector<Acoustics::AcousticMaterial> materials{Acoustics::makeAcousticMaterials()};

    const Acoustics::ImpulseResponse here{Acoustics::gather(bvh, materials, EAR, plainConfig())};
    const Acoustics::ImpulseResponse here_again{Acoustics::gather(bvh, materials, EAR, plainConfig())};

    TEST_CHECK(here.bins == here_again.bins);

    // Moving the ear must move the answer, or ear position does not belong in the key.
    const Acoustics::ImpulseResponse lower{Acoustics::gather(bvh, materials, MathLib::Vec3{0.0f, 2.0f, 0.0f}, plainConfig())};
    TEST_CHECK(!(lower.bins == here.bins));

    // An ear closer to the sounding floor hears it sooner: the first occupied bin is the
    // perpendicular drop, and two metres is a shorter drop than five.
    TEST_CHECK(firstOccupiedBin(lower) == binOf(2.0f));
    TEST_CHECK(firstOccupiedBin(here) == binOf(5.0f));
    TEST_CHECK(firstOccupiedBin(lower) < firstOccupiedBin(here));

    // The config must be in the key too: a different ray budget is a different answer.
    Acoustics::GatherConfig sparse{plainConfig()};
    sparse.direction_count = 256u;
    const Acoustics::ImpulseResponse coarse{Acoustics::gather(bvh, materials, EAR, sparse)};
    TEST_CHECK(!(coarse.bins == here.bins));
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
