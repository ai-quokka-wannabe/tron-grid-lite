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

#include "acoustics.hpp"
#include "roster.hpp"
#include "stage.hpp"

#include <bvh/bvh.hpp>
#include <math/vector.hpp>
#include <tgl/tgl_program_abi.h>

#include <array>
#include <cstdint>
#include <vector>

/*!
    Answers radiance for a batch of sample rays.

    An interface for the same reason `SensesSource` is one: what stands behind it needs a device —
    `SensesTracer` dispatches `senses.slang` — and this file is compiled into targets that must
    never mention one. The GPU-free tests implement it with arithmetic instead, which is what makes
    the whole eye path below testable under ctest.
*/
class RadianceSolver {
public:
    virtual ~RadianceSolver() = default;

    RadianceSolver(const RadianceSolver&) = delete;
    RadianceSolver& operator=(const RadianceSolver&) = delete;
    RadianceSolver(RadianceSolver&&) = delete;
    RadianceSolver& operator=(RadianceSolver&&) = delete;

    /*!
        Replaces the placements the next solves trace against.

        The per-tick half of the world: geometry never moves in the shared buffers once uploaded,
        so a tick owes the solver only these records — including any the caller has blanked to a
        zero node count, which is how a creature's own body is made invisible to its own eyes
        without the shader learning a skip. The default does nothing, which is correct for a
        solver whose arithmetic has no instances, and for a run whose placements never move.
    */
    virtual void stage(const std::vector<BvhLib::InstanceRecord>& instances)
    {
        (void)instances;
    }

    /*!
        Traces one batch of sample rays and returns linear radiance per ray.

        \param rays Origin, direction interleaved: ray i is elements 2i and 2i + 1, w ignored.
        \param max_bounces Total ray segments per sample, at least 1.
        \return One radiance triple per ray, in ray order.
    */
    [[nodiscard]] virtual std::vector<MathLib::Vec4> solve(const std::vector<MathLib::Vec4>& rays, uint32_t max_bounces) = 0;

protected:
    RadianceSolver() = default;
};

/*!
    Ray segments per eye or irradiance sample.

    The same depth the User's window renders at (`MAX_BOUNCES` in main.cpp), deliberately: a
    creature and the User standing in the same place must see the same Grid, and a different bounce
    budget would be a second, quieter way for their pictures to disagree.
*/
inline constexpr uint32_t SENSES_MAX_BOUNCES{6u};

/*!
    The senses source for a run with a Grid: what a creature's sensors read from the world.

    **Ears are answered on the host**, by the same `Acoustics::gather` the verification modes hold
    the device to. That is deliberate rather than provisional: the gather is a pure function, its
    output is already the ABI's band-major shape, and a host answer keeps the ear half of the tick
    loop runnable on a machine with no device at all. Calls ride the same path: every vocalisation
    sounding this tick is delivered onto every ear's gathered hum by `Acoustics::deliverCall`,
    caller's own ears included — which is how a creature hears itself, and the whole of what
    echolocation is.

    **Eyes and irradiance are answered by the `RadianceSolver`** — one flat batch of sample rays
    per creature per pose, eye samples first and irradiance directions after, all traced by the
    very `radiance` function the User's window renders with. A body declaring either sense while
    no solver is attached is refused loudly rather than left silently unseeing: a Program cannot
    tell a dark Grid from an unbuilt sense.

    No Vulkan in this file, which is what keeps everything it does testable under ctest.
*/
class GridSensesSource final : public RosterLib::SensesSource {
public:
    /*!
        \param scene The Grid. Borrowed; must outlive this source.
        \param source_strengths Acoustic source strength per material slot, as
               `Acoustics::makeAcousticSourceStrengths` builds it. Taken by value and kept.
        \param reflectors Mirror planes a call's early reflections are enumerated against. Taken by
               value and kept. An empty set is a Grid whose calls echo off nothing, which is the
               honest default for a scene nobody described.
        \param radiance_solver Answers eye and irradiance rays. May be null, in which case a body
               declaring either sense is refused. Borrowed; must outlive this source.
        \param stage The creatures' standing in the scene. May be null for a run whose bodies never
               move and never occlude — every test that predates bodies. When present, `scene` must
               be the stage's own scene, because the instances this source skips are indices into
               it. Borrowed; must outlive this source.
    */
    GridSensesSource(const BvhLib::Scene& scene, std::vector<float> source_strengths, Acoustics::Reflectors reflectors = {}, RadianceSolver* radiance_solver = nullptr,
        Stage* stage = nullptr);

    /*!
        Collects the tick's calls: who is sounding, from where, and how loudly.

        Read from the roster physics has settled rather than from staged intent, because staged
        copies are overwritten one by one as Programs run and a mid-loop read would hear a tick
        that never happened. Cheap by construction — a handful of comparisons — and cleared every
        tick, so a tick nobody announces is a silent one, which keeps every fill outside a roster
        exactly as it was before calls existed.
    */
    void beginTick(const std::vector<RosterLib::Creature>& creatures) override;

    /*!
        The guests this tick: the other bodies in the world, where they stand and whether they
        call. Told before `beginTick`, which places them on the stage, counts their motion as
        motion the caches must respect, and lets their calls reach the hosted ears - from the
        guest's own position, seeing through its own hull. A world with no guests tells none.
    */
    void tellGuests(std::vector<Stage::GuestTelling> guests);

    /*!
        Fills the traced senses for one creature.

        A stationary creature is answered from its previous solves rather than re-traced: the
        gather and the radiance walk are both pure functions of the Grid and the sensor's world
        placement, so while the pose has not changed the cached answer is not an approximation of
        the right one — it *is* the right one. The Grid and the configs are fixed for this source's
        lifetime, which leaves the pose as the whole key, compared exactly.

        Calls are the deliberate exception to the caching: they are events rather than state, so
        on a tick with any call sounding, every ear receives its cached hum plus every call's
        delivery, computed fresh — an enumeration of a few dozen candidates per call, against the
        gather's thousands of rays. The hum cache itself is never written to by a call, which is
        what keeps the skip licence exact.
    */
    void fill(const RosterLib::Creature& creature, TglSenses& senses) override;

private:
    /*!
        One ear's gathered response, in storage that honours the ABI's alignment promise.

        `TglEarView::energy` is documented 16-byte aligned, and `Acoustics::ImpulseResponse` makes
        no such promise — so the response is copied into this rather than pointed at in place.
    */
    struct alignas(16) AlignedResponse {
        std::array<float, Acoustics::BAND_COUNT * Acoustics::BIN_COUNT> energy{};
    };

    //! The key of one ear's last solve: where it was solved, and whether it ever was.
    struct EarKey {
        MathLib::Vec3 world_position{};
        bool solved{false};
    };

    /*!
        Every ear of one creature, keyed by the creature's stable id.

        Keys and responses are parallel vectors rather than one struct, because a struct holding an
        over-aligned member is padded to that alignment and MSVC reports the padding as a warning
        this repository builds with as an error.

        `responses` is the hum cache and only ever holds a pure gather; `delivered` is scratch for
        ticks with calls, holding hum plus deliveries. Two buffers rather than one, because a call
        written into the cache would be replayed to a stationary ear for ever — the skip licence is
        exact only while the cached answer is exactly the gather's.
    */
    struct CreatureEars {
        uint64_t creature_id{0u};
        std::vector<EarKey> keys;
        std::vector<AlignedResponse> responses;
        std::vector<AlignedResponse> delivered;
    };

    //! One call sounding this tick: where it left from, how loudly, and whose body to see through.
    struct Call {
        MathLib::Vec3 position{};
        float strength{0.0f};
        uint32_t caller_instance{BvhLib::NO_INSTANCE};
    };

    //! Four floats at the alignment `TglEyeView::samples` promises; eye storage is a vector of these.
    struct AlignedFloat4 {
        alignas(16) float v[4];
    };

    //! Everything one creature last saw, and the pose it saw it from.
    struct CreatureVision {
        uint64_t creature_id{0u};
        MathLib::Vec3 position{};
        float yaw{0.0f};
        bool solved{false};
        std::vector<std::vector<AlignedFloat4>> eye_samples; //!< One aligned block per eye.
        float irradiance{0.0f};
    };

    //! Returns this creature's ear states, creating them on first sight.
    [[nodiscard]] CreatureEars& earStateFor(uint64_t creature_id, uint32_t ear_count);

    //! Returns this creature's vision state, creating it on first sight.
    [[nodiscard]] CreatureVision& visionStateFor(const TglCreatureDesc& body);

    void fillEars(const RosterLib::Creature& creature, TglSenses& senses);
    void fillVision(const RosterLib::Creature& creature, TglSenses& senses);

    const BvhLib::Scene& m_scene; //!< The Grid. Non-owning.
    std::vector<float> m_source_strengths; //!< Acoustic strength per material slot.
    Acoustics::Reflectors m_reflectors; //!< Mirror planes for a call's early reflections.
    RadianceSolver* m_radiance_solver; //!< Answers eye and irradiance rays. Non-owning; may be null.
    Stage* m_stage; //!< The bodies' standing in the scene. Non-owning; may be null.

    /*!
        True while any body has moved since the last tick, which stales every traced-sense cache.

        The caches key on the listener's own pose because that used to be everything a solve read;
        a body standing in the scene is read too, so a tick on which one moved must re-solve even
        for a listener that held still. Bodiless rosters never set it, and pay nothing.
    */
    bool m_bodies_moved{false};
    std::vector<RosterLib::Pose> m_last_body_poses; //!< One per modelled creature, in roster order.

    std::vector<CreatureEars> m_ear_states; //!< Found by linear search; a roster is a handful of creatures.
    std::vector<CreatureVision> m_vision_states;
    std::vector<TglEarView> m_ear_views; //!< Wired into TglSenses; overwritten by the next fill.
    std::vector<TglEyeView> m_eye_views;
    std::vector<Call> m_calls; //!< The calls sounding this tick. Rebuilt by every beginTick.
    std::vector<Stage::GuestTelling> m_guests; //!< The world's other bodies this tick.
};
