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

#include "roster.hpp"

#include "acoustics.hpp"

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace
{

    /*!
        The first body's audiogram, and the storage its descriptors borrow for the whole run.

        Static rather than per-creature, because every creature currently is this one body, and the
        descriptor copy the roster keeps carries pointers that must outlive every tick.

        Four bands, edged so that each contains exactly one harmonic of the Grid's 3 kHz hum — which
        is what makes `GatherConfig`'s unit spectrum the correct resolution of that hum into these
        bands, with nothing to compute. A preset body with a real audiogram computes its own; this
        body's hearing was chosen to make the arithmetic legible while the loop is being trusted.

        Air absorption is zero, which the gather documents as a legitimate specification: below a
        few kilohertz air is very nearly transparent across the 20 m range cap, and a non-zero row
        here would be a number invented rather than evaluated from ISO 9613-1.
    */
    constexpr std::array<float, Acoustics::BAND_COUNT + 1u> FIRST_BODY_BAND_EDGES_HZ{2000.0f, 4500.0f, 7500.0f, 10500.0f, 13500.0f};
    constexpr std::array<float, Acoustics::BAND_COUNT> FIRST_BODY_AIR_ABSORPTION_DB_PER_KM{0.0f, 0.0f, 0.0f, 0.0f};

    /*!
        Two ears, twenty centimetres ahead of and behind the body origin along its own -Z.

        Two rather than one because the path-length difference between them is the entire physical
        basis the ABI offers for localisation, and an ear pair that shared a position would collapse
        it. The bin count and width are the gather's own, asserted by initialisation: a body asking
        for different ones would need a resampling stage nothing requires yet.
    */
    constexpr std::array<TglEarDesc, 2u> FIRST_BODY_EARS{
        TglEarDesc{
            .band_edges_hz = FIRST_BODY_BAND_EDGES_HZ.data(),
            .air_absorption_db_per_km = FIRST_BODY_AIR_ABSORPTION_DB_PER_KM.data(),
            .position = {0.0f, 0.0f, -0.2f},
            .band_count = Acoustics::BAND_COUNT,
            .bin_count = Acoustics::BIN_COUNT,
            .bin_seconds = Acoustics::BIN_SECONDS,
        },
        TglEarDesc{
            .band_edges_hz = FIRST_BODY_BAND_EDGES_HZ.data(),
            .air_absorption_db_per_km = FIRST_BODY_AIR_ABSORPTION_DB_PER_KM.data(),
            .position = {0.0f, 0.0f, 0.2f},
            .band_count = Acoustics::BAND_COUNT,
            .bin_count = Acoustics::BIN_COUNT,
            .bin_seconds = Acoustics::BIN_SECONDS,
        },
    };

    /*!
        Two eyes of one sample each, at the same stations as the ears.

        The head sample looks forward along the body's own -Z and the tail sample backward along
        +Z — the arrangement of the simplest preset in PERCEPTION.md, whose head and tail
        photoreceptors sitting in different places is the only spatial light discrimination it has.

        One channel, because these are intensity receptors with no colour opinion. The acceptance
        angle is the eye's true integration width; the render currently answers it with a single
        ray, which delivers the aliasing PERCEPTION.md rule 4 asks for rather than the blur rule 6
        eventually wants. Quantisation is stated as zero because the Grid does not quantise yet,
        and claiming four bits before the mechanism exists would be prose pretending to be code.
    */
    constexpr std::array<float, 3u> FIRST_BODY_HEAD_EYE_DIRECTION{0.0f, 0.0f, -1.0f};
    constexpr std::array<float, 3u> FIRST_BODY_TAIL_EYE_DIRECTION{0.0f, 0.0f, 1.0f};
    constexpr std::array<float, 1u> FIRST_BODY_EYE_ACCEPTANCE_RADIANS{0.5235988f};

    constexpr std::array<TglEyeDesc, 2u> FIRST_BODY_EYES{
        TglEyeDesc{
            .sample_directions = FIRST_BODY_HEAD_EYE_DIRECTION.data(),
            .sample_acceptance_angles = FIRST_BODY_EYE_ACCEPTANCE_RADIANS.data(),
            .position = {0.0f, 0.0f, -0.2f},
            .sample_count = 1u,
            .channels = 1u,
            .quantisation_bits = 0u,
        },
        TglEyeDesc{
            .sample_directions = FIRST_BODY_TAIL_EYE_DIRECTION.data(),
            .sample_acceptance_angles = FIRST_BODY_EYE_ACCEPTANCE_RADIANS.data(),
            .position = {0.0f, 0.0f, 0.2f},
            .sample_count = 1u,
            .channels = 1u,
            .quantisation_bits = 0u,
        },
    };

    //! The body every creature gets until glTF bodies arrive. One rigid piece; two ears, two eyes.
    [[nodiscard]] TglCreatureDesc firstBody(uint64_t creature_id) noexcept
    {
        TglCreatureDesc desc{};
        desc.creature_id = creature_id;

        /*
            Derived from the roster index rather than drawn, so that the same roster produces the
            same seeds on every run and two creatures of it never share one. There is no run seed to
            mix in yet; when there is, it mixes in here and nowhere else.
        */
        desc.random_seed = 0x9E3779B97F4A7C15ull ^ (creature_id * 0x1000193ull);

        desc.eyes = FIRST_BODY_EYES.data();
        desc.ears = FIRST_BODY_EARS.data();
        desc.eye_count = static_cast<uint32_t>(FIRST_BODY_EYES.size());
        desc.ear_count = static_cast<uint32_t>(FIRST_BODY_EARS.size());

        // Sixty-four rays for the sphere integral: enough that the mean is stable against the
        // Fibonacci set's stride, and two orders of magnitude below what one window pixel costs.
        desc.irradiance_sample_count = 64u;
        desc.max_contact_count = 0u;

        /*
            A metre a second and a right angle a second. Chosen to be legible in a log rather than
            from any animal: at this turn rate a creature asking for full lock comes about in four
            seconds, which is slow enough to read tick by tick while the loop is being trusted.

            desired_vertical_speed is bounded at zero, which clamps it away entirely. That is the
            honest specification for a body on a floor with nothing to climb, and it means a Program
            asking to fly is told no by the same mechanism that tells it how fast it may walk.
        */
        desc.max_forward_speed = 1.0f;
        desc.max_turn_rate = 1.5707964f;
        desc.max_vertical_speed = 0.0f;
        desc.max_vocalisation_strength = 1.0f;

        return desc;
    }

    //! Zero if the value is not a real number. The first half of sanitise-then-clamp.
    [[nodiscard]] float finiteOrZero(float value) noexcept
    {
        return std::isfinite(value) ? value : 0.0f;
    }

    [[nodiscard]] float clampMagnitude(float value, float bound) noexcept
    {
        if (value > bound) {
            return bound;
        }
        if (value < -bound) {
            return -bound;
        }
        return value;
    }

} // namespace

namespace RosterLib
{

    MathLib::Vec3 forwardFor(float yaw) noexcept
    {
        /*
            -Z is forward at rest and +Y is up, right-handed, so a positive yaw rotates -Z towards
            -X: a turn to the creature's left seen from above, which is what the ABI promises of a
            positive desired_turn_rate.
        */
        return MathLib::Vec3{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    }

    MathLib::Vec3 worldFromBody(const Pose& pose, const MathLib::Vec3& body_point) noexcept
    {
        /*
            The same rotation forwardFor applies to -Z, applied to an arbitrary point: a right-handed
            yaw about +Y carries (x, z) to (x cos + z sin, z cos - x sin). At yaw zero this is the
            identity, so a sensor's body position is simply an offset from where the body stands.
        */
        const float sin_yaw{std::sin(pose.yaw)};
        const float cos_yaw{std::cos(pose.yaw)};
        return pose.position + MathLib::Vec3{(body_point.x * cos_yaw) + (body_point.z * sin_yaw), body_point.y, (body_point.z * cos_yaw) - (body_point.x * sin_yaw)};
    }

    MathLib::Vec3 worldDirectionFromBody(const Pose& pose, const MathLib::Vec3& body_direction) noexcept
    {
        const float sin_yaw{std::sin(pose.yaw)};
        const float cos_yaw{std::cos(pose.yaw)};
        return MathLib::Vec3{(body_direction.x * cos_yaw) + (body_direction.z * sin_yaw), body_direction.y, (body_direction.z * cos_yaw) - (body_direction.x * sin_yaw)};
    }

    void sanitiseAndClamp(TglActions& actions, const TglCreatureDesc& desc) noexcept
    {
        actions.desired_forward_speed = clampMagnitude(finiteOrZero(actions.desired_forward_speed), desc.max_forward_speed);
        actions.desired_turn_rate = clampMagnitude(finiteOrZero(actions.desired_turn_rate), desc.max_turn_rate);
        actions.desired_vertical_speed = clampMagnitude(finiteOrZero(actions.desired_vertical_speed), desc.max_vertical_speed);

        // A call is loudness rather than a signed quantity, so a negative one is silence rather than
        // something to be reflected into the positive half.
        const float vocalisation{finiteOrZero(actions.vocalisation_strength)};
        actions.vocalisation_strength = (vocalisation < 0.0f) ? 0.0f : clampMagnitude(vocalisation, desc.max_vocalisation_strength);
    }

    Roster::Roster(const std::filesystem::path& directory, std::string_view identifier, uint32_t creature_count) :
        m_library(directory, identifier, TglLibraryInfo{creature_count == 0u ? 1u : creature_count, TICK_SECONDS})
    {
        if (creature_count == 0u) {
            throw std::runtime_error{"A roster of no creatures has nothing to tick."};
        }

        m_creatures.reserve(creature_count);
        for (uint32_t index{0u}; index < creature_count; ++index) {
            const TglCreatureDesc desc{firstBody(index)};

            TglProgram* const program{m_library.vtable().program_rez(&desc)};
            if (program == nullptr) {
                throw std::runtime_error{"Program \"" + m_library.identifier() + "\" refused to rez creature " + std::to_string(index) + "."};
            }

            Creature creature;
            creature.program = program;
            creature.body = desc;

            // Spaced along +X so that two creatures do not begin inside one another. A body has no
            // extent yet, so this is a convention waiting for a reason rather than a clearance.
            creature.pose.position = MathLib::Vec3{static_cast<float>(index) * 2.0f, 0.0f, 0.0f};

            m_creatures.push_back(creature);
        }
    }

    Roster::~Roster()
    {
        /*
            Derez every creature before the library is shut down, because a Program is entitled to
            touch its own state in program_derez and not after library_shutdown. The library's own
            destructor makes the second call, so the ordering here is the whole of what this has to
            get right — and it is why m_library is declared first and therefore destroyed last.

            Wrapped because a destructor is implicitly noexcept: anything escaping terminates the
            process outright, with no handler anywhere able to intervene.
        */
        try {
            for (Creature& creature : m_creatures) {
                if (creature.program != nullptr) {
                    m_library.vtable().program_derez(creature.program);
                    creature.program = nullptr;
                }
            }
        } catch (...) {
        }
    }

    void Roster::tick(SensesSource& senses_source)
    {
        for (Creature& creature : m_creatures) {
            TglSenses senses{};
            senses.tick = m_tick;
            senses.dt_seconds = TICK_SECONDS;

            // What the actuators reported last tick.
            senses.body_forward_speed = creature.forward_speed;
            senses.body_vertical_speed = creature.vertical_speed;
            senses.body_turn_rate = creature.turn_rate;

            /*
                A body at rest in gravity, which is what an otolith reads when nothing is
                accelerating it. The linear part arrives with the solver: this body's velocity
                changes the instant a Program asks, so its true acceleration is unbounded and there
                is no honest number to report for it yet.
            */
            senses.specific_force[1] = 9.81f;

            // This one is already true. The body really is turning at the rate it was given.
            senses.angular_velocity[1] = creature.turn_rate;

            // The traced senses: eyes, ears, irradiance. Contacts wait for physics.
            senses_source.fill(creature, senses);

            TglActions actions{};
            m_library.vtable().program_tick(creature.program, &senses, &actions);

            sanitiseAndClamp(actions, creature.body);

            creature.forward_speed = actions.desired_forward_speed;
            creature.vertical_speed = actions.desired_vertical_speed;
            creature.turn_rate = actions.desired_turn_rate;

            /*
                Kinematics rather than dynamics: the body goes exactly where it was asked to, at the
                speed it was allowed. Nothing pushes back, nothing falls, and the yaw is integrated
                before the step so that a turning creature moves along the arc it is on rather than
                the one it has left.
            */
            creature.pose.yaw += creature.turn_rate * TICK_SECONDS;

            const MathLib::Vec3 forward{forwardFor(creature.pose.yaw)};
            creature.pose.position += forward * (creature.forward_speed * TICK_SECONDS);
            creature.pose.position.y += creature.vertical_speed * TICK_SECONDS;
        }

        ++m_tick;
    }

    const std::vector<Creature>& Roster::creatures() const noexcept
    {
        return m_creatures;
    }

    uint64_t Roster::tickCount() const noexcept
    {
        return m_tick;
    }

} // namespace RosterLib
