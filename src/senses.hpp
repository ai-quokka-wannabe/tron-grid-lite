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
    loop runnable on a machine with no device at all.

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
        \param radiance_solver Answers eye and irradiance rays. May be null, in which case a body
               declaring either sense is refused. Borrowed; must outlive this source.
    */
    GridSensesSource(const BvhLib::Scene& scene, std::vector<float> source_strengths, RadianceSolver* radiance_solver = nullptr);

    /*!
        Fills the traced senses for one creature.

        A stationary creature is answered from its previous solves rather than re-traced: the
        gather and the radiance walk are both pure functions of the Grid and the sensor's world
        placement, so while the pose has not changed the cached answer is not an approximation of
        the right one — it *is* the right one. The Grid and the configs are fixed for this source's
        lifetime, which leaves the pose as the whole key, compared exactly.
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
    */
    struct CreatureEars {
        uint64_t creature_id{0u};
        std::vector<EarKey> keys;
        std::vector<AlignedResponse> responses;
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
    RadianceSolver* m_radiance_solver; //!< Answers eye and irradiance rays. Non-owning; may be null.

    std::vector<CreatureEars> m_ear_states; //!< Found by linear search; a roster is a handful of creatures.
    std::vector<CreatureVision> m_vision_states;
    std::vector<TglEarView> m_ear_views; //!< Wired into TglSenses; overwritten by the next fill.
    std::vector<TglEyeView> m_eye_views;
};
