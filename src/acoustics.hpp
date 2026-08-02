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

#pragma once

#include "components.hpp"
#include <bvh/bvh.hpp>
#include <math/vector.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

/*!
    Hearing on the Grid: the acoustic half of "one Grid, two senses".

    Sound is traced through the very same hierarchy as light, with the same slab test and the same
    Möller-Trumbore intersection — `libs/bvh` on the host and `grid_bvh.slang` on the device. What
    differs is everything downstream of the intersection, and the difference is not a detail:

    - **A visual ray's answer is a value; an acoustic ray's answer is a function of time.** Light is
      instantaneous for a renderer. Sound travels at 343 m/s, so a ray carries an accumulated path
      length, and where that length lands in time is as much of the answer as how loud it is.
    - **Path length, not depth, is the termination rule.** A ray dies when it has travelled further
      than the range cap. Reflection order is a second, looser bound.
    - **The output is a histogram, not a pixel.** Energy is deposited into a time bin per band, and
      many rays land in the same bin.

    Everything here is deterministic. Directions come from a spherical Fibonacci set indexed by ray
    number, never from a random number generator: there is no RNG anywhere in this repository, and
    `docs/PROGRAM_INTERFACE.md` publishes bit-identical replay as a guarantee. A quasi-uniform set
    gives the same coverage for the same count, identically, every run.

    **`gather` is a pure function, and that is a load-bearing property rather than a tidiness one.**
    The same Grid, materials, ear and config give a bit-identical response, which means a solve
    whose inputs have not changed may simply be skipped — and the answer that was skipped is not an
    approximation of the right one, it *is* the right one. A stationary creature in a static Grid
    hears exactly what it heard last tick, so re-solving is not cheap-and-approximate, it is
    expensive-and-pointless.

    This is a stronger licence than the renderer has. The debug view has to compare the camera state
    it drew from, because floating-point integration of a held key can wander by a bit and produce a
    genuinely different frame; a gather cannot wander, so its cache key is exact. Whatever eventually
    drives solves must key on the Grid's generation, the ear's position and the config — and
    `src/tests/acoustics_tests.cpp` pins that every one of those three can change the answer, which
    is what makes them the key rather than an arbitrary choice.

    This header is the specification. `acoustics.slang` mirrors it on the GPU, exactly as
    `trace.slang` mirrors `libs/bvh`, and `src/tests/acoustics_tests.cpp` holds this side to its own
    arithmetic. The design and its citations live in `docs/ACOUSTICS.md`.
*/
namespace Acoustics
{

    /*!
        Frequency bands the response is resolved into.

        Four, for the reason Schissler and Manocha give for their four: a band vector fits one
        `float4`. Offline room acoustics uses seven or eight octave bands and every shipping
        real-time system uses far fewer — Steam Audio uses three. Four is also generous against the
        biology, which tops out at one band for a moth and no frequency discrimination at all for a
        nematode.

        The band *edges* are not fixed here. They come from a listener's audiogram and differ per
        creature, which is why every function below takes the per-band constants as parameters
        rather than computing them.
    */
    inline constexpr uint32_t BAND_COUNT{4u};

    /*!
        Time bins in a delivered response.

        Sixty-four bins of `BIN_SECONDS` covers the range cap with room to spare, and the whole
        histogram is 64 × 4 × 4 bytes = 1 KiB — comfortably inside the 16 KiB of shared memory
        Vulkan guarantees a workgroup, which is what lets one workgroup own one ear and removes
        cross-workgroup atomic contention entirely.
    */
    inline constexpr uint32_t BIN_COUNT{64u};

    /*!
        Width of one time bin, in seconds.

        One millisecond, not the four that MathWorks' Audio Toolbox and pyroomacoustics both
        default to. That convention exists to keep a *stochastic tail* smooth on a modest ray
        budget, and this scene has no tail — it has a handful of resolvable arrivals, and what
        matters is spatial resolution. A bin is a distance resolution: 1 ms is 17.2 cm of
        out-and-back range, which is a creature-scale number; 4 ms is 69 cm, which is not.
    */
    inline constexpr float BIN_SECONDS{0.001f};

    //! Speed of sound in the Grid's air, in metres per second. Twenty degrees, as everything here assumes.
    inline constexpr float SPEED_OF_SOUND{343.0f};

    /*!
        Total accumulated path a ray may travel before it is abandoned, in metres.

        Justified three ways in `docs/ACOUSTICS.md`: air absorption at 30 kHz costs 14 dB over 20 m
        and 90 dB over 128 m, so the Grid has a physical acoustic horizon rather than a budgetary
        one; Schnitzler and Kalko bound useful echolocation at 10.5 m; and a distance cap bounds
        traversal cost predictably. **The Grid is smaller acoustically than visually, and that is
        physics rather than a budget.**

        This bounds *total accumulated path*, not one-way range, so a monostatic out-and-back echo
        reaches half of it.
    */
    inline constexpr float RANGE_METRES{20.0f};

    //! Reflections a ray may make before it is abandoned. Rays escape after one or two here; four is generous.
    inline constexpr uint32_t MAX_ORDER{4u};

    //! Distance to nudge a reflected ray off the surface it left, in metres. Matches trace.slang.
    inline constexpr float SURFACE_EPSILON{1e-3f};

    /*!
        A surface's acoustic properties. Two floats, and that is the whole model.

        This is not a cheap approximation of a fuller one. The optical model is physically-based
        rendering at the smooth limit, where the microfacet distribution collapses to a delta
        function and the BRDF reduces analytically to Fresnel plus Snell; the acoustic model has the
        same character — the correct closed form for the geometry the Grid actually contains, which
        is flat hard specular surfaces and nothing else.

        Keeping it at two is also a response to the input data. Treble's documentation states that
        measured absorption coefficients "can have errors of about ±0.2", which for a hard surface
        at 0.03 is a nominal uncertainty several times the value itself. The input data are worse
        than the algorithms, and that is a licence to keep the model small rather than a defect to
        engineer around.
    */
    struct AcousticMaterial {
        /*!
            Fraction of incident energy the surface swallows per reflection, from 0 to 1.

            Broadband: one number, no bands. Put the decay in proportion before adding any — at
            `absorption = 0.02`, ten bounces cost 0.88 dB, while spherical spreading over the same
            128 m costs about 42 dB and air absorption at 8 kHz costs 9.8 dB. **Surface absorption
            is the smallest term in the entire model by an order of magnitude.**
        */
        float absorption{0.0f};

        /*!
            How loudly this surface radiates the Grid's hum, relative to a primary neon tube.

            Authored separately from `Material::emission` rather than derived from it, and this is
            the whole reason the acoustic table exists: the pillars and the glowing column are
            optically emissive and acoustically silent. A gather keyed on optical emission would
            find 16,724 triangles where the design calls for 16,640.

            A scalar rather than a spectrum because every sounding surface on the Grid radiates the
            *same* spectrum — what differs between one and another is how loudly, not with what
            colour. The spectrum is a Grid-level constant supplied per solve.
        */
        float source_strength{0.0f};
    };

    static_assert(sizeof(AcousticMaterial) == 8u, "AcousticMaterial must be two tightly packed floats for the std430 layout.");
    static_assert(alignof(AcousticMaterial) == 4u, "AcousticMaterial must be 4-byte aligned so the array is tightly packed.");

    /*!
        Returns the Grid's acoustic material table, in `MaterialSlot` order.

        The values and their provenance are tabulated in `docs/ACOUSTICS.md`. In short: everything
        is a hard surface, so absorption is 0.02 or 0.03 throughout, and only the two neon tube
        materials sing. The primary tube is the unit of the source scale — there is no reference
        level on the Grid at all, so the number is relative by construction.
    */
    [[nodiscard]] std::vector<AcousticMaterial> makeAcousticMaterials();

    /*!
        An impulse response: energy per band per time bin, at one ear.

        Bin `b` of band `k` holds everything that arrived between `b * BIN_SECONDS` and
        `(b + 1) * BIN_SECONDS` after the sound left its source. Bin zero is the direct arrival's
        home only for a source less than 34 cm away; on this Grid it is usually empty.
    */
    struct ImpulseResponse {
        //! Band-major: `bins[(band * BIN_COUNT) + bin]`.
        std::array<float, BAND_COUNT * BIN_COUNT> bins{};

        //! Returns the energy in one bin of one band.
        [[nodiscard]] float at(uint32_t band, uint32_t bin) const
        {
            return bins[(static_cast<size_t>(band) * BIN_COUNT) + bin];
        }

        //! Returns a reference to one bin of one band.
        [[nodiscard]] float& at(uint32_t band, uint32_t bin)
        {
            return bins[(static_cast<size_t>(band) * BIN_COUNT) + bin];
        }

        //! Returns the total energy across every band and bin.
        [[nodiscard]] float total() const;
    };

    //! What one solve needs to know that is not the Grid and not the listener's position.
    struct GatherConfig {
        /*!
            The hum's strength in each of the listener's bands.

            One spectrum for the whole Grid: a 3 kHz fundamental and its harmonics, resolved into
            whichever bands the listening ear happens to have. Three kilohertz because the
            intersection of *C. elegans* (100 Hz – 5 kHz) and the house mouse (2.3 – 85.5 kHz) is
            2.3 – 5 kHz, and it is the only window in which the simplest and the most acoustically
            capable creatures on the roster can hear the same sound.
        */
        std::array<float, BAND_COUNT> hum_spectrum{{1.0f, 1.0f, 1.0f, 1.0f}};

        //! Air absorption in each band, dB per kilometre. See `airAbsorptionDbPerKm`.
        std::array<float, BAND_COUNT> air_absorption_db_per_km{{0.0f, 0.0f, 0.0f, 0.0f}};

        //! Directions cast from the ear. Must be at least one.
        uint32_t direction_count{2048u};

        //! Reflections a ray may make. Zero casts direct rays only.
        uint32_t max_order{MAX_ORDER};

        //! Total accumulated path cap, in metres.
        float range_metres{RANGE_METRES};
    };

    /*!
        Returns direction `index` of a spherical Fibonacci set of `count` directions.

        Quasi-uniform over the whole sphere, deterministic, and cheap: no random number generator,
        no table, and the same set every run on every machine. The construction places the `i`th
        point at height `1 - 2(i + 0.5)/n` and rotates it by the golden angle each step, which
        spaces the points about as evenly as any closed-form set on a sphere can.

        \param index Which direction, from 0 to `count - 1`.
        \param count How many directions the set holds. Must be at least one.
        \return A unit vector.
    */
    [[nodiscard]] MathLib::Vec3 fibonacciDirection(uint32_t index, uint32_t count);

    /*!
        Returns atmospheric absorption at a frequency, in decibels per kilometre.

        Tabulated from ISO 9613-2's 20 °C / 70 % relative humidity row at the eight octave centres
        from 63 Hz to 8 kHz, and interpolated logarithmically in both frequency and level between
        them. The Grid is fixed at those conditions, so nothing evaluates a relaxation-frequency
        model at run time; if temperature ever becomes a scene parameter, that is when ISO 9613-1's
        formulae move into the code.

        **Above 8 kHz this extrapolates and must not be trusted.** The tabulated curve is steepening
        towards `f²` at its top end and the extrapolation continues that, which is the right
        asymptotic shape and the wrong number — the real curve turns over as the relaxation terms
        saturate. It is enough for the `elegans` and `macropod` presets, whose bands stop at 5 and
        40 kHz, and it is *not* enough for the `rodent`, whose top band reaches 85.5 kHz. Before a
        rodent listens, ISO 9613-1 must be evaluated offline at the octave centres above 8 kHz and
        the constants pasted into the table beside the tabulated ones, exactly as
        `docs/ACOUSTICS.md` specifies. That is recorded as an open item rather than done here,
        because no creature exists yet to be wronged by it.

        \param frequency_hz Frequency of interest. Must be positive.
        \return Absorption in dB/km.
    */
    [[nodiscard]] float airAbsorptionDbPerKm(float frequency_hz);

    /*!
        Traces the Grid from one ear and returns what it hears.

        A **gather**: rays leave the ear rather than the sources. The literature is unanimous that
        acoustics scatters — rays leave the source and are collected at listeners — and the stated
        reason is that one cannot backward-trace to a known arrival time, because arrival time is
        what is being computed. That reasoning does not apply to a deterministic specular path:
        path length is symmetric, so a gathered ray that accumulates distance as it goes knows its
        own delay when it lands. And the Grid's sound comes from 16,640 neon tubes, so a
        source-driven cast would have to iterate all of them; a gather casts a fixed budget from the
        one place that matters.

        Spreading is applied **explicitly as 1/r²**, never as a detection sphere. The two are
        mutually exclusive and mixing them double-counts spreading; detection spheres exist to make
        a stochastic ray count converge in a closed room, and the Grid is an open plane with a point
        receiver it can afford. The failure mode of getting this wrong is a silent 6 dB per doubling
        that looks like a material problem.

        \param bvh The Grid. An empty one returns silence.
        \param materials Acoustic table indexed by a triangle's material. Must cover every index used.
        \param ear World-space position of the ear, in metres.
        \param config Spectrum, air absorption, ray budget and caps.
        \return Energy per band per time bin.
    */
    [[nodiscard]] ImpulseResponse gather(const BvhLib::Bvh& bvh, const std::vector<AcousticMaterial>& materials, const MathLib::Vec3& ear, const GatherConfig& config);

} // namespace Acoustics
