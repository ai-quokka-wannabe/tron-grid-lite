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
#include <functional>
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

    //! Half the body's height, metres: how far its origin stands above what it stands on.
    inline constexpr float BODY_HALF_HEIGHT{0.05f};

    //! Half the body's length along its own Z, metres. The ears and eyes at ±0.2 sit just inside.
    inline constexpr float BODY_HALF_LENGTH{0.215f};

    /*!
        Height of the ground under a point, in world metres.

        A function rather than geometry, because the Grid's floor is an analytic surface before it
        is triangles — `gridMeshHeight` is the ground truth the floor mesh was generated from, and
        physics collides against the truth rather than against its tessellation. The run mode binds
        the Grid's own surface; tests bind whatever terrain the case needs. The roster promises to
        call it only with finite coordinates, and expects the same value for the same input every
        time — replay stands on that.
    */
    using GroundFunction = std::function<float(float x, float z)>;

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

    struct Creature;

    /*!
        A body's validated shape, copied out of the Program's borrowed `TglRenderModel` at rez.

        Owned storage rather than borrowed pointers, because the ABI's arrays die with the rez call
        and the shape has to outlive every tick. Kept in the ABI's own element types so that what
        was accepted is exactly what was offered; the world's triangle form is derived from this
        when the body is staged into the scene, not here, because staging owes the world global
        material indices this struct cannot know.
    */
    struct CreatureModel {
        std::vector<MathLib::Vec3> vertex_positions;
        std::vector<TglRenderTriangle> triangles;
        std::vector<TglRenderMaterial> materials;

        //! True when the body has no visible shape, which is a legitimate body and today's default.
        [[nodiscard]] bool empty() const noexcept
        {
            return triangles.empty();
        }
    };

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

        /*!
            Announces the tick's roster-wide context before any creature is filled.

            Called once per tick, after the bodies' state has settled for the tick — the world's
            telling, once Master Control's physics owns motion — and before the first `fill`. A
            fill sees one listener, but some of what a listener senses is a fact about the whole
            roster — who is calling this tick, and eventually where every body stands — so the
            source reads it here, from a settled roster, rather than piecing it together from
            per-creature calls whose staged state is being overwritten as the Programs run. The
            default does nothing, which is the correct behaviour for a source with no
            cross-creature senses.
        */
        virtual void beginTick(const std::vector<Creature>& creatures)
        {
            (void)creatures;
        }

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

        //! The shape the Program offered at rez and the Grid accepted, validated whole. Empty for
        //! a Program that offered none, which every fixture but the modelled one is.
        CreatureModel model{};

        Pose pose{};

        //! World-frame velocity, owned by the world's physics — Master Control's, since the
        //! simulation followed its owner out; a hosted body carries what the telling said.
        MathLib::Vec3 velocity{};

        //! True while the body stands on the ground. Traction is a fact about contact: intent
        //! moves the body only while this is true.
        bool grounded{false};

        //! What the actuators are actually doing. This is the proprioception a Program reads back,
        //! and it disagrees with what was asked for whenever a bound bit or the feet left the floor.
        float forward_speed{0.0f};
        float turn_rate{0.0f};

        //! What the voice is doing this tick: the loudness of the call sounding now, zero for a
        //! silent body. Applied from the staged intent at the top of the tick so that every ear
        //! on the roster hears one consistent tick — with no traction condition, because a body
        //! calls as well in flight as standing. There is no proprioceptive readback for it: the
        //! caller hears its own call, which is the readback an animal actually has.
        float vocalisation{0.0f};

        /*!
            The intent the world acts on next tick, staged raw: the host's clamp was convenience
            and Master Control's is the law — the server sanitises every intent before its
            physics sees it.

            Staged rather than applied, because the lifecycle promises that an action takes effect
            on the next tick for every creature alike — applying inside the Program loop would let
            the first creature's action move the world before the last creature's call.
        */
        TglActions staged{};

        //! What the body felt this tick, in body frame: the otolith's reading, and every contact.
        MathLib::Vec3 specific_force{};
        std::vector<TglContact> contacts;
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
            \param ground Height of the ground under a point. Bodies rez standing on it.
            \throws std::runtime_error if the library will not load, or if a rez returns NULL.
        */
        Roster(const std::filesystem::path& directory, std::string_view identifier, uint32_t creature_count, GroundFunction ground);

        ~Roster();

        Roster(const Roster&) = delete;
        Roster& operator=(const Roster&) = delete;
        Roster(Roster&&) = delete;
        Roster& operator=(Roster&&) = delete;

        /*!
            One tick of the mind's half of the lifecycle — all that lives here since the physics
            followed its owner to Master Control.

            The voice actuator is applied from last tick's staged intent first, so the calls
            sounding this tick are a fact about everyone before anyone listens; the senses source
            is told the settled roster through `beginTick`; then each creature in roster order
            receives its senses — the bodily ones here, the traced ones from `senses_source` —
            its Program is called, and its actions are staged raw for the world to validate and
            act on. An action therefore takes effect on the next tick for every creature alike,
            which is the promise `docs/PROGRAM_INTERFACE.md` § Lifecycle makes. Poses advance
            only by the world's telling: the wire host that carries it arrives as its own etape.

            \param senses_source Fills eyes, ears and irradiance per creature. Storage it wires in
                   is valid for that creature's `program_tick` call only.
        */
        void tick(SensesSource& senses_source);

        /*!
            The world's telling for one creature: where it is and how it moves, as Master
            Control's physics settled it. The actuators' proprioception follows from it - the
            forward speed is the velocity along the body's facing, the turn rate is the yaw
            rate - because that is what the body actually did, which is what a Program reads back.
        */
        void tellPose(uint32_t index, const Pose& pose, const MathLib::Vec3& velocity, float yaw_rate);

        //! The owner's letter for one creature: what the body felt this tick, in body frame.
        void tellFeel(uint32_t index, bool grounded, const MathLib::Vec3& specific_force, std::vector<TglContact> contacts);

        [[nodiscard]] const std::vector<Creature>& creatures() const noexcept;

        //! Ticks elapsed since the run began. Shared by every creature.
        [[nodiscard]] uint64_t tickCount() const noexcept;

    private:
        ProgramLib::Library m_library;
        GroundFunction m_ground;
        std::vector<Creature> m_creatures;
        uint64_t m_tick{0u};
    };

} // namespace RosterLib
