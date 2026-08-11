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

#include "program_library.hpp"

#include <math/vector.hpp>
#include <tgl/tgl_program_abi.h>

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

/*!
    The creatures on the Grid, and the loop that gives each of them a turn.

    **No Vulkan here either**, for the third time and the same reason: a Program driving a body is a
    complete thing to test, and it is testable without a device only if nothing in the path mentions
    one. Senses arrive from the tracers later; what this owns is the tick.
*/
namespace RosterLib
{

    /*!
        Seconds per tick, and the only place the number lives on the Grid's side.

        **32 Hz because 0.03125 is exact in binary32**, and so is the 0.0078125 of a four-substep
        tick when a solver eventually wants one. That makes `tick * dt` exact for a hundred and
        forty-five hours, so a recording's timestamps are the same numbers on the way out as on the
        way in. 25 Hz would give 0.04 and 50 Hz 0.02, neither of which is representable, and both of
        which accumulate an error somebody eventually has to explain in a replay.

        The rate is never sized to what looks smooth through the debug window. That window is the one
        part a human perceives, which is exactly why it must not drive the decision.
    */
    inline constexpr float TICK_SECONDS{0.03125f};

    //! The same fact as an integer, for anything that wants to say "per second" without dividing.
    inline constexpr uint32_t TICKS_PER_SECOND{32u};

    static_assert(TICK_SECONDS * static_cast<float>(TICKS_PER_SECOND) == 1.0f,
        "The tick length and the tick rate are two spellings of one number and have drifted apart. Both must be exact in binary32.");

    static_assert(TICK_SECONDS / 4.0f * 4.0f == TICK_SECONDS, "A four-substep tick must divide exactly, or a physics step will not land on the tick boundary.");

    /*!
        Where a body is and which way it faces.

        Position and a single yaw, because the first body turns and moves and does nothing else. A
        quaternion arrives with the solver that can produce a pitch worth storing; carrying one now
        would be three unused components and a normalisation nobody performs.
    */
    struct Pose {
        MathLib::Vec3 position{};

        //! Radians about +Y, right-handed, so increasing yaw turns to the creature's left.
        float yaw{0.0f};
    };

    //! The direction a body faces at a given yaw, in world space. Unit length.
    [[nodiscard]] MathLib::Vec3 forwardFor(float yaw) noexcept;

    //! A body-frame point carried into world space by a pose: rotated by the yaw, then translated.
    //! This is how a sensor's position on the body becomes the place it senses from.
    [[nodiscard]] MathLib::Vec3 worldFromBody(const Pose& pose, const MathLib::Vec3& body_point) noexcept;

    //! A body-frame direction carried into world space: the rotation alone, with no translation.
    //! This is how an eye sample's view direction becomes the way it actually looks.
    [[nodiscard]] MathLib::Vec3 worldDirectionFromBody(const Pose& pose, const MathLib::Vec3& body_direction) noexcept;

    /*!
        Replaces non-finite values with zero and then clamps to the body's bounds.

        **What must be true is that a NaN request becomes zero and never a bound.** A NaN velocity
        becomes a NaN position and then a hierarchy traversal that does not terminate, so a Program
        returning garbage would take the Grid down rather than merely behave oddly — and a NaN
        quietly promoted to `max_forward_speed` would be worse still, because the creature would
        sprint and nothing would look wrong.

        Sanitise precedes clamp because the ABI fixes that order, and mutation testing is worth
        recording here: with the comparison-based clamp below the order is *not* observable, since
        every comparison against NaN is false and the NaN falls through to be caught either way. It
        becomes observable the moment somebody rewrites the clamp with `fmin`/`fmax`, which return
        the non-NaN operand and would turn garbage into a legal-looking bound. The order is kept so
        that rewrite stays safe; the test asserts the outcome rather than the order, because the
        outcome is what matters.

        \param actions What the Program asked for. Modified in place.
        \param desc The body, which carries the bounds.
    */
    void sanitiseAndClamp(TglActions& actions, const TglCreatureDesc& desc) noexcept;

    struct Creature;

    /*!
        Fills the senses only the tracers can answer: eyes, ears and irradiance.

        An interface rather than a member, because what stands behind it differs by run mode: the
        real one holds the Grid's geometry, the GPU-free tests hold a stub, and the roster's own
        promise — no Vulkan — is kept by never knowing which it was handed.

        Everything a fill wires into `senses` is storage the source owns, borrowed for the one
        `program_tick` call exactly as the ABI specifies, and overwritten by the next fill. A source
        is called once per creature per tick, in roster order, on the tick thread.
    */
    class SensesSource {
    public:
        virtual ~SensesSource() = default;

        SensesSource(const SensesSource&) = delete;
        SensesSource& operator=(const SensesSource&) = delete;
        SensesSource(SensesSource&&) = delete;
        SensesSource& operator=(SensesSource&&) = delete;

        //! Fills eyes, ears and irradiance for one creature. The kinematic senses are already set.
        virtual void fill(const Creature& creature, TglSenses& senses) = 0;

    protected:
        SensesSource() = default;
    };

    //! A source for runs with nothing to sense: every traced sense stays at its zeroed default,
    //! which the ABI defines as a body with no eyes, no ears and no thermoreception.
    class NullSensesSource final : public SensesSource {
    public:
        NullSensesSource() = default;

        void fill(const Creature&, TglSenses&) override
        {
        }
    };

    //! One creature: the body the Grid moves, and the Program handle that drives it.
    struct Creature {
        TglProgram* program{nullptr};

        //! This creature's own body. The Grid keeps a copy because the descriptor handed to
        //! program_rez is borrowed for that call only, and the bounds are needed every tick.
        TglCreatureDesc body{};

        Pose pose{};

        //! What the actuators are actually doing, after clamping. This is the proprioception a
        //! Program reads back, and it disagrees with what was asked for whenever a bound bit.
        float forward_speed{0.0f};
        float vertical_speed{0.0f};
        float turn_rate{0.0f};
    };

    /*!
        A loaded Program library and the creatures it drives.

        One library for the whole roster rather than one per creature, because that is what the ABI
        describes: `library_init` once, then a `program_rez` per body. The roster is fixed at startup,
        so `TglLibraryInfo::creature_count` is exact rather than a hint.

        Owns the lifecycle in both directions. Construction loads, initialises and rezzes; destruction
        derezzes every creature and then shuts the library down, in that order, because a Program is
        entitled to touch its own state in `program_derez` and not after `library_shutdown`.
    */
    class Roster {
    public:
        /*!
            \param directory Directory the Grid trusts to hold Program libraries.
            \param identifier Name of the Program, not a path.
            \param creature_count How many bodies it will drive. At least one.
            \throws std::runtime_error if the library will not load, or if a rez returns NULL.
        */
        Roster(const std::filesystem::path& directory, std::string_view identifier, uint32_t creature_count);

        ~Roster();

        Roster(const Roster&) = delete;
        Roster& operator=(const Roster&) = delete;
        Roster(Roster&&) = delete;
        Roster& operator=(Roster&&) = delete;

        /*!
            Gives every creature one turn, in roster order.

            Senses are assembled — the kinematic ones here, the traced ones by `senses_source` — the
            Program is called, its actions are sanitised and clamped, and the body is moved. Nothing
            here consults another creature's state, so roster order is not yet load-bearing — and
            the moment it would be, that is a change worth arguing about rather than discovering.

            \param senses_source Fills eyes, ears and irradiance per creature. Storage it wires in
                   is valid for that creature's `program_tick` call only.
        */
        void tick(SensesSource& senses_source);

        [[nodiscard]] const std::vector<Creature>& creatures() const noexcept;

        //! Ticks elapsed since the run began. Shared by every creature.
        [[nodiscard]] uint64_t tickCount() const noexcept;

    private:
        ProgramLib::Library m_library;
        std::vector<Creature> m_creatures;
        uint64_t m_tick{0u};
    };

} // namespace RosterLib
