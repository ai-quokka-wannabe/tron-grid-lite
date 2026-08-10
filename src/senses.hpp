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
    The senses source for a run with a Grid: what a creature's sensors read from the world.

    **Ears are answered on the host**, by the same `Acoustics::gather` the verification modes hold
    the device to. That is deliberate rather than provisional: the gather is a pure function, its
    output is already the ABI's band-major shape, and a host answer keeps the whole tick loop — and
    therefore the future per-tick state hash — runnable on a machine with no device at all.

    **Eyes and irradiance are not filled here yet.** Both are radiance questions, radiance lives in
    `trace.slang`, and the device pass that asks it per sample ray arrives separately. A body with
    eyes handed to this source today is refused loudly rather than silently unseeing.

    No Vulkan in this file, and that is the point of it being separate from the tracers it will
    later coordinate: everything it does today is testable under ctest.
*/
class GridSensesSource final : public RosterLib::SensesSource {
public:
    /*!
        \param scene The Grid. Borrowed; must outlive this source.
        \param source_strengths Acoustic source strength per material slot, as
               `Acoustics::makeAcousticSourceStrengths` builds it. Taken by value and kept.
    */
    GridSensesSource(const BvhLib::Scene& scene, std::vector<float> source_strengths);

    /*!
        Fills the traced senses for one creature.

        A stationary ear is answered from the previous solve rather than re-gathered: the gather is
        a pure function of the Grid, the ear's world position and the config, so while none of those
        has changed the cached answer is not an approximation of the right one — it *is* the right
        one. The Grid and the config are fixed for this source's lifetime, which leaves the world
        position as the whole key, compared exactly.
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

    //! Returns this creature's ear states, creating them on first sight.
    [[nodiscard]] CreatureEars& stateFor(uint64_t creature_id, uint32_t ear_count);

    const BvhLib::Scene& m_scene; //!< The Grid. Non-owning.
    std::vector<float> m_source_strengths; //!< Acoustic strength per material slot.
    std::vector<CreatureEars> m_ear_states; //!< Found by linear search; a roster is a handful of creatures.
    std::vector<TglEarView> m_ear_views; //!< Wired into TglSenses; overwritten by the next fill.
};
