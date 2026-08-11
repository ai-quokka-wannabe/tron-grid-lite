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
    The same Grid, source strengths, ear and config give a bit-identical response, which means a solve
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
        The golden angle as a fraction of a full turn: (3 - sqrt(5)) / 2.

        A literal rather than a computation, and `acoustics.slang` carries the same one. Deriving it
        from `sqrt(5)` on each side invites the two libraries to disagree in the last bit, which would
        put every ray on a slightly different heading.

        **It lives in the header so that it can be asserted against the shader's copy.** A file-local
        constant in `acoustics.cpp` would be out of reach of the `static_assert`s in
        `acoustic_tracer.cpp` that hold `BIN_SECONDS`, `SPEED_OF_SOUND` and `SURFACE_EPSILON` to their
        Slang twins, leaving the one duplicated constant every ray direction depends on as the one
        with nothing holding the copies together — under the tightest verification threshold in the
        repository. That is the exact pattern CLAUDE.md names as the source of nearly every serious
        defect here.
    */
    inline constexpr float GOLDEN_TURN_FRACTION{0.38196601125010515f};

    /*!
        Every surface on the Grid is a perfect acoustic mirror.

        No absorption, no transmission, no scattering and no frequency dependence: a surface returns
        all of the energy that reaches it, at the mirror angle, and that is the whole surface
        response. There is consequently no acoustic *material* table at all — only the source
        strengths below, which say what a surface emits rather than what it does to what hits it.

        This is the same choice the optical model makes, for the same reason. That model is not a
        cheap approximation of physically-based rendering; it is PBR at the smooth limit, where the
        microfacet distribution collapses to a delta function and the BRDF reduces analytically to
        Fresnel-weighted mirror reflection plus Snell refraction. The acoustic model is likewise the
        correct closed form for what the Grid actually contains, which is flat hard specular
        surfaces and nothing else.

        **Absorption is left out on its own numbers, not for simplicity.** At the `alpha = 0.02` of a
        polished hard surface, ten bounces cost 0.88 dB — and on an open plane rays escape after one
        or two, so the realistic figure is nearer 0.2 dB. Against spherical spreading's 26 dB across
        the range cap that is the smallest term in the model by an order of magnitude, and Treble's
        documentation puts the measurement uncertainty on such a coefficient at about ±0.2, which is
        larger than the effect it would produce. A term whose error bar exceeds its value is
        decoration rather than physics.

        **Transmission was never modelled, and its absence is a decision rather than an oversight.**
        Sound does pass through a glass slab in reality. Representing that honestly would need a
        thickness, a transmission coefficient and an interface model — the acoustic counterpart of
        exactly the microfacet machinery the optical side deliberately does without. On the Grid a
        slab is an obstacle, and behind one there is quiet.

        **The regime in which this is correct, written down so it can be checked later.** A lossless
        reflector is only safe because the Grid is an open half-space: rays leave and do not come
        back, total path is capped at `RANGE_METRES`, and reflection order is capped at `MAX_ORDER`,
        so nothing can accumulate without bound. Enclose any part of the Grid — a room, a tunnel, a
        lid over a terrace hollow — and perfect mirrors would ring forever. That is the condition
        under which this decision has to be reopened, and it is a geometry decision rather than an
        acoustic one.
    */

    /*!
        Returns how loudly each material radiates the Grid's hum, in `MaterialSlot` order.

        Authored rather than derived from `Material::emission`, and that is the whole reason this
        table exists: the pillars and the glowing column are optically emissive and acoustically
        silent, so a gather keyed on optical emission would find 16,724 triangles where the design
        calls for 16,640. Only the two neon tube materials sing, and they sing equally — they are
        identical hardware differing in gas colour, which is an optical property and not an acoustic
        one.

        A scalar rather than a spectrum, because every sounding surface on the Grid radiates the
        *same* spectrum. What differs between one and another is how loudly, not with what colour,
        so the spectrum is a Grid-level constant supplied once per solve and this table carries only
        the number that multiplies it. The primary tube is the unit of that scale: there is no
        reference level anywhere on the Grid, so the value is relative by construction.
    */
    [[nodiscard]] std::vector<float> makeAcousticSourceStrengths();

    /*!
        An impulse response: energy per band per time bin, at one ear.

        Bin `b` of band `k` holds everything that arrived between `b * BIN_SECONDS` and
        `(b + 1) * BIN_SECONDS` after the sound left its source. Bin zero is the direct arrival's
        home only for a source less than 34 cm away; on this Grid it is usually empty.

        **This is an impulse response and not a signal, and the distinction is the whole reason
        nothing on the Grid needs a notion of time inside the gather.** It answers "if this source
        fired an impulse now, where in time does its energy arrive", which is a property of geometry
        alone. What a source actually does in time — a pulse, a one-shot call, a scrape — is an
        envelope that multiplies this at delivery, and the traversal never sees it.

        That separation is what lets the Grid hold to a rule it does hold to: **nothing on the Grid
        sounds continuously.** The neon pulses rather than holding a tone. A creature vocalising
        emits a call and stops, as an animal does. A worm dragging itself across the floor scrapes —
        sustained, but noisy and modulated by its own gait rather than held at a level. All three are
        this same object with different envelopes.

        The reason the rule exists is perceptual rather than computational. **A continuous tone
        carries almost no delay information**: every arrival overlaps every other and the listener
        receives a steady level with nothing to measure, which would make the millimetre-scale
        ambitions of `BIN_SECONDS` describe something no creature could extract. Onsets are what make
        a delay measurable, which is exactly why bats pulse instead of humming. It also yields the
        right asymmetry for nothing: a scraping worm is easy to detect and hard to range, because a
        sustained noisy source has no sharp onset to range from.
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
            The source's strength in each of the listener's bands, **at full amplitude**.

            One spectrum for the whole Grid: a 3 kHz fundamental and its harmonics, resolved into
            whichever bands the listening ear happens to have. Three kilohertz because the
            intersection of *C. elegans* (100 Hz – 5 kHz) and the house mouse (2.3 – 85.5 kHz) is
            2.3 – 5 kHz, and it is the only window in which the simplest and the most acoustically
            capable creatures on the roster can hear the same sound.

            "At full amplitude" is the load-bearing phrase. Nothing on the Grid sounds continuously
            — see `ImpulseResponse` — so this is the spectrum a source has while it is sounding, not
            an average over time and not a level it holds.
        */
        std::array<float, BAND_COUNT> hum_spectrum{{1.0f, 1.0f, 1.0f, 1.0f}};

        /*!
            Atmospheric absorption in each band, in decibels per kilometre.

            Authored per listener rather than computed, for the same reason the band edges are: both
            follow from the audiogram, and a creature that hears in a different place needs different
            numbers in both. There is deliberately no function here that turns a frequency into an
            absorption — the Grid is fixed at 20 °C and 70 % humidity, so the values are constants,
            and a constant is better written down where somebody can see it than derived by an
            interpolation that has to be trusted.

            ISO 9613-2's row for those conditions, at the octave centres, is the source: 0.1, 0.3,
            1.1, 2.8, 5.0, 9.0, 22.9 and 76.6 dB/km at 63 Hz through 8 kHz. Above 8 kHz the standard
            tabulates nothing and ISO 9613-1's formulae have to be evaluated — which is a thing to do
            once, offline, and paste in, not a thing to guess at. That matters for anything
            ultrasonic: this is the term that gives the Grid a physical acoustic horizon, since past
            about 7 m a 100 kHz call loses more to the air than to the inverse square law, while at
            8 kHz the same crossover lies out beyond 400 m.

            Zero is a legitimate value and means a listener for whom air is transparent over the
            distances involved, which is very nearly true below a few kilohertz across 20 m.
        */
        std::array<float, BAND_COUNT> air_absorption_db_per_km{{0.0f, 0.0f, 0.0f, 0.0f}};

        //! Directions cast from the ear. Must be at least one.
        uint32_t direction_count{2048u};

        //! Reflections a ray may make. Zero casts direct rays only.
        uint32_t max_order{MAX_ORDER};

        //! Total accumulated path cap, in metres.
        float range_metres{RANGE_METRES};

        /*!
            Scene instance this gather's rays pass through, `BvhLib::NO_INSTANCE` for none.

            The listener's own body: an ear sits on or in its creature's hull, and a hull that
            blocked its own ears would deafen the body it belongs to. Excluding one instance is the
            honest first cut of self-hearing — what is lost with it is the creature's own head
            shadow, which needs the sensors modelled proud of the hull, and that is the body
            author's business.
        */
        uint32_t skip_instance{BvhLib::NO_INSTANCE};
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

        Surfaces reflect losslessly, so nothing here attenuates on contact. What bounds the
        response instead is the range cap, the reflection order cap, spreading and air — see the
        note on the perfect acoustic mirror above for why that is safe on an open plane and where it
        would stop being safe.

        \param bvh The Grid. An empty one returns silence.
        \param source_strengths How loudly each material sings, indexed by a triangle's material.
               Must cover every index the Grid uses.
        \param ear World-space position of the ear, in metres.
        \param config Spectrum, air absorption, ray budget and caps.
        \return Energy per band per time bin.
    */
    [[nodiscard]] ImpulseResponse gather(const BvhLib::Bvh& bvh, const std::vector<float>& source_strengths, const MathLib::Vec3& ear, const GatherConfig& config);

    /*!
        The same gather over a scene of placed geometries.

        The general form, and the one the other overload is written in terms of — a lone hierarchy is
        a scene of one instance at the identity, which is exactly what it is on the device too. There
        is therefore one gather here rather than two that must be kept in step.

        This is what holds `acoustics.slang` to a specification when something is placed at an angle.
        A gather cannot be checked against a rotated copy of itself: the direction set is built in
        **world** space, so rotating the world re-aims all of the rays at different geometry, and a
        finite fan then samples a genuinely different set of paths. Two implementations sampling the
        *same* fan against the *same* placement is the comparison that means something.

        \param scene Geometries and the instances placing them.
        \param source_strengths Acoustic source strength per material slot.
        \param ear World-space position of the ear, in metres.
        \param config Spectrum, air absorption, ray budget and caps.
        \return Energy per band per time bin.
    */
    [[nodiscard]] ImpulseResponse gather(const BvhLib::Scene& scene, const std::vector<float>& source_strengths, const MathLib::Vec3& ear, const GatherConfig& config);

    /*!
        How closely a struck surface must align with a mirror plane before an image-source arrival
        is believed, as the absolute dot product of the two unit normals.

        Every facet of the Grid is horizontal or vertical, so a genuine mirror agrees with its
        plane to float rounding and everything else disagrees by a right angle. What the guard
        refuses is a surface merely passing through the reflection point — a riser standing where
        a terrace level's mirror arithmetic put its bounce, or a terrace top where a wall's did —
        which would deliver an echo computed with the wrong mirror's geometry.
    */
    inline constexpr float MIRROR_ALIGNMENT_MINIMUM{0.999f};

    /*!
        One rectangular reflector: a corner and the two full edges that span the face.

        The outward normal is `cross(edge_u, edge_v)` normalised, so the winding of the edges is the
        statement of which side reflects. The edges must be perpendicular to each other — a box face
        always is, and the in-rectangle test below assumes it.
    */
    struct RectFace {
        MathLib::Vec3 origin{};
        MathLib::Vec3 edge_u{};
        MathLib::Vec3 edge_v{};
    };

    /*!
        The mirror planes a point source's early reflections are enumerated against.

        An enumeration rather than a search, because a point source cannot be gathered: a ray fan
        from the ear has vanishing probability of passing through a point, so the paths from a call
        must be constructed — source, mirror image, reflection point — and then *validated* against
        the real geometry with rays. The list is therefore candidates, not facts: a level with no
        triangle under the reflection point, or a face with something in the way, contributes
        nothing, and the validation ray is the plausibility test. That is also why the list may be
        generous — `gridTerraceLevels` includes the top level the quantisation can only reach where
        the noise is exactly one, and on a landscape that never reaches it the candidate simply
        never validates.

        First order only. The Grid's own arithmetic rules out going further: six levels and fifty
        faces are fifty-six candidates, and their second order is over three thousand, almost all of
        them geometrically impossible. What first order misses — riser paths, oblique multi-bounce —
        is exactly what the gather already covers for extended sources, and a call's late energy is
        below the air and spreading floors regardless.
    */
    struct Reflectors {
        //! Heights of horizontal mirror planes: the floor's terrace levels.
        std::vector<float> level_heights;

        //! Rectangular mirror faces: the outward faces of everything standing on the floor.
        std::vector<RectFace> faces;
    };

    /*!
        The five outward faces of an axis-aligned box: four sides and the top.

        The bottom face is deliberately absent. A box on the Grid stands on the floor — `plantOnFloor`
        sets it *into* the ground where a terrace step crosses its footprint — so its underside faces
        earth rather than air, and a reflector nothing can reach is a candidate that costs validation
        rays and returns nothing.

        \param centre World-space centre of the box, in metres.
        \param half_extents Half the size along each axis.
        \return Five faces whose outward normals point away from the centre.
    */
    [[nodiscard]] std::array<RectFace, 5u> outwardBoxFaces(const MathLib::Vec3& centre, const MathLib::Vec3& half_extents);

    //! What one call delivery needs to know that is not the Grid, the caller or the listener's position.
    struct CallConfig {
        /*!
            The call's strength in each of the listener's bands, at full loudness.

            What a voice sounds like is a fact about the emitting body, resolved into whichever
            bands the listening ear happens to have — the same split the hum makes between its
            Grid-level spectrum and the ear's edges. The unit default is the first body's white
            call heard by the first body's ears; a preset body with a real voice resolves its own
            spectrum, and that resolver arrives with it.
        */
        std::array<float, BAND_COUNT> spectrum{{1.0f, 1.0f, 1.0f, 1.0f}};

        //! Atmospheric absorption in each of the listener's bands, in decibels per kilometre.
        //! Authored per listener, exactly as `GatherConfig` documents.
        std::array<float, BAND_COUNT> air_absorption_db_per_km{{0.0f, 0.0f, 0.0f, 0.0f}};

        //! Total accumulated path cap, in metres. The same physical horizon the gather has.
        float range_metres{RANGE_METRES};

        /*!
            Radius of the sphere the direct-occlusion probe samples, in metres.

            The single largest error available in this subsystem is "ray blocked implies silence":
            in a ray model a thin post between source and listener occludes as much as a wall. The
            probe samples points on a sphere of this radius around the source and reports the
            *fraction* of them reachable, so a graze dims the direct arrival instead of severing
            it. **It is not diffraction and must never be described as diffraction** — it removes
            the discontinuity, and the discontinuity is the artefact that would break a creature's
            behaviour. The default is the first body's half height; pass the emitting body's own
            scale.
        */
        float source_radius_metres{0.05f};

        //! Points the occlusion probe samples. One, or a zero radius, degrades to a single
        //! binary ray. Half a dozen is the number that turns a hard shadow into a graded one.
        uint32_t occlusion_sample_count{6u};

        /*!
            Scene instances this delivery's rays pass through, `BvhLib::NO_INSTANCE` for none.

            The caller's body and the listener's body — the call leaves from inside the caller's
            hull and arrives at an ear on or in the listener's, so a delivery that let either hull
            block its own rays would gag every voice and deafen every ear the moment bodies became
            real. Everyone else's body occludes honestly, which is most of what a body is for.
        */
        uint32_t caller_instance{BvhLib::NO_INSTANCE};
        uint32_t listener_instance{BvhLib::NO_INSTANCE};
    };

    /*!
        Delivers one creature's call to one ear: the point-source half of hearing on the Grid.

        The opposite mechanism to `gather`, for the opposite kind of source. The hum is extended
        geometry a ray fan cannot miss; a call is a point a ray fan cannot hit. So where the gather
        casts and collects, this **enumerates**: the direct path, graded by the occlusion probe
        rather than cut by it, and one first-order image per reflector — the source mirrored in the
        plane, the reflection point constructed, and both legs validated with rays through the very
        hierarchy every other sense reads. A candidate whose reflection point has no triangle under
        it, whose struck surface is not aligned with the mirror plane, or whose path is blocked
        contributes nothing.

        Same units, same bins, same spreading floor and same air model as the gather, applied by the
        same arithmetic — the returned response adds bin for bin onto a gathered one, which is how a
        tick's ear view carries the hum and every call at once. Surfaces reflect losslessly here for
        the same reason they do there.

        The response is the Grid's answer for the whole flight of the call: an echo later than the
        listener's window is dropped, exactly as `TglEarDesc` documents, and with the range cap at
        `RANGE_METRES` of total path nothing this function can produce falls off the end. A
        monostatic echo therefore reaches half the cap in range — an emitter hears its own wall at
        ten metres, not twenty — which is the physics of the out-and-back path rather than a second
        budget.

        \param scene The Grid. Validation rays are traced against it.
        \param reflectors Candidate mirror planes, as `Reflectors` documents.
        \param source World-space position the call leaves from, in metres.
        \param strength Loudness of the call, in the unit `TglEarView::energy` defines. Zero or
               negative is silence and returns an empty response.
        \param ear World-space position of the listening ear, in metres.
        \param config Spectrum, air absorption, range cap and the occlusion probe.
        \return Energy per band per time bin, ready to add onto a gathered response.
    */
    [[nodiscard]] ImpulseResponse deliverCall(const BvhLib::Scene& scene, const Reflectors& reflectors, const MathLib::Vec3& source, float strength,
        const MathLib::Vec3& ear, const CallConfig& config);

} // namespace Acoustics
