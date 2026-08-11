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
#include <numbers>
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

        // Support underfoot, a landing, a wall, and one spare. A standing body reports the floor
        // every tick, so zero here would be a body numb to the world it rests on.
        desc.max_contact_count = 4u;

        /*
            A metre a second and a right angle a second. Chosen to be legible in a log rather than
            from any animal: at this turn rate a creature asking for full lock comes about in four
            seconds, which is slow enough to read tick by tick while the loop is being trusted.
        */
        desc.max_forward_speed = 1.0f;
        desc.max_turn_rate = std::numbers::pi_v<float> / 2.0f;
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

        // A call is loudness rather than a signed quantity, so a negative one is silence rather than
        // something to be reflected into the positive half.
        const float vocalisation{finiteOrZero(actions.vocalisation_strength)};
        actions.vocalisation_strength = (vocalisation < 0.0f) ? 0.0f : clampMagnitude(vocalisation, desc.max_vocalisation_strength);
    }

    Roster::Roster(const std::filesystem::path& directory, std::string_view identifier, uint32_t creature_count, GroundFunction ground) :
        m_library(directory, identifier, TglLibraryInfo{creature_count == 0u ? 1u : creature_count, TICK_SECONDS}),
        m_ground(std::move(ground))
    {
        if (creature_count == 0u) {
            throw std::runtime_error{"A roster of no creatures has nothing to tick."};
        }

        if (!m_ground) {
            throw std::runtime_error{"A roster needs ground for its bodies to stand on."};
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
            const float x{static_cast<float>(index) * 2.0f};

            // Rezzed standing rather than dropped: a body that spawns in the air arrives with a
            // landing thump on tick one, which would make the first recorded contact an artefact of
            // rezzing rather than a fact about the world.
            creature.pose.position = MathLib::Vec3{x, m_ground(x, 0.0f) + BODY_HALF_HEIGHT, 0.0f};
            creature.grounded = true;

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

    /*!
        Advances one body by one tick of physics.

        **Analytic where a closed form exists, symplectic where it does not, impulses at the
        non-smooth points.** Ballistic flight under constant gravity has an exact solution — which
        is also precisely what velocity-Verlet produces for a constant force, so the closed form
        and the symplectic scheme coincide with zero integration error. A body moving at constant
        speed while turning at constant rate traces a circular arc, and the arc has a closed form
        too. Contacts are the places smoothness dies: no closed form and no order statement
        survives a trajectory being clipped, so they resolve as impulses — project to the surface,
        change the velocity, report what was felt. Higher-order schemes would buy nothing here,
        because their error bounds assume exactly the smoothness a contact destroys.
    */
    void stepBody(Creature& creature, const GroundFunction& ground)
    {
        constexpr float dt{TICK_SECONDS};
        constexpr float TURN_EPSILON{1e-6f};

        creature.contacts.clear();

        const MathLib::Vec3 velocity_before{creature.velocity};
        const MathLib::Vec3 position_before{creature.pose.position};

        // Traction is a fact about contact: on the ground the actuators command their velocities
        // directly; in the air the body keeps the velocity and spin it left the ground with.
        if (creature.grounded) {
            creature.forward_speed = creature.staged.desired_forward_speed;
            creature.turn_rate = creature.staged.desired_turn_rate;
        }

        // The voice has no traction condition: a body calls as well in flight as standing, so the
        // staged loudness simply is what the voice does this tick. Setting it here, in the physics
        // pass that runs for the whole roster before any senses are filled, is what gives every
        // ear one consistent answer to who is calling — the staged copies are overwritten one by
        // one as the Programs run, and anything reading them mid-loop would hear a tick that never
        // happened.
        creature.vocalisation = creature.staged.vocalisation_strength;

        float x{position_before.x};
        float z{position_before.z};
        const float yaw_before{creature.pose.yaw};
        const float yaw_after{yaw_before + (creature.turn_rate * dt)};

        if (creature.grounded) {
            const float speed{creature.forward_speed};
            const float turn{creature.turn_rate};

            if ((turn > TURN_EPSILON) || (turn < -TURN_EPSILON)) {
                /*
                    The exact arc: with forward = (-sin yaw, -cos yaw), integrating speed * forward
                    over the tick gives the closed form below. The approximation this replaces —
                    integrate the yaw, then move straight along the new heading — walks a chord of
                    the arc and drifts outward a little every tick; the closed form does not drift
                    at all, which matters to a replay measured in bits.
                */
                x += (speed / turn) * (std::cos(yaw_after) - std::cos(yaw_before));
                z -= (speed / turn) * (std::sin(yaw_after) - std::sin(yaw_before));
            } else {
                const MathLib::Vec3 forward{forwardFor(yaw_before)};
                x += forward.x * speed * dt;
                z += forward.z * speed * dt;
            }
        } else {
            // Ballistic horizontally: straight at the velocity it left the ground with.
            x += velocity_before.x * dt;
            z += velocity_before.z * dt;
        }

        // Ballistic vertical motion, in closed form: exact for constant gravity.
        float y{position_before.y + (velocity_before.y * dt) - (0.5f * GRAVITY * dt * dt)};
        float velocity_y{velocity_before.y - (GRAVITY * dt)};

        /*
            A terrace riser taller than ankle height is a wall. The horizontal move is cancelled —
            the turn is kept, since nothing stops a body swivelling against a step — and the stop
            is felt on the front face, which is how the lattice becomes something a creature can
            feel and count rather than glide over.
        */
        if (creature.grounded) {
            const float rise{ground(x, z) - ground(position_before.x, position_before.z)};
            if (rise > CLIMB_LIMIT_METRES) {
                const float arrested{creature.forward_speed};
                x = position_before.x;
                z = position_before.z;
                creature.forward_speed = 0.0f;

                if (arrested != 0.0f) {
                    // Pushed backward along the body's own +Z, however the body is facing.
                    TglContact wall{};
                    wall.position[2] = -BODY_HALF_LENGTH;
                    wall.impulse[2] = BODY_MASS_KG * arrested;
                    creature.contacts.push_back(wall);
                }
            }
        }

        // The ground claims everything at or below standing height.
        const float standing{ground(x, z) + BODY_HALF_HEIGHT};
        if (y <= standing) {
            const float arrested{-velocity_y};

            // One contact under the feet carrying the whole normal impulse of the tick: the
            // support that holds a standing body up, plus whatever arrested a landing.
            TglContact foot{};
            foot.position[1] = -BODY_HALF_HEIGHT;
            foot.impulse[1] = (BODY_MASS_KG * GRAVITY * dt) + (creature.grounded ? 0.0f : BODY_MASS_KG * arrested);
            creature.contacts.push_back(foot);

            y = standing;
            velocity_y = 0.0f;
            creature.grounded = true;
        } else {
            creature.grounded = false;
        }

        creature.pose.position = MathLib::Vec3{x, y, z};
        creature.pose.yaw = yaw_after;

        if (creature.grounded) {
            const MathLib::Vec3 forward{forwardFor(yaw_after)};
            creature.velocity = MathLib::Vec3{forward.x * creature.forward_speed, velocity_y, forward.z * creature.forward_speed};
        } else {
            creature.velocity = MathLib::Vec3{velocity_before.x, velocity_y, velocity_before.z};

            // In the air the actuator drives nothing; what proprioception honestly reports is the
            // forward component of the motion the body actually has.
            creature.forward_speed = velocity_before.x * forwardFor(yaw_after).x + velocity_before.z * forwardFor(yaw_after).z;
        }

        /*
            Specific force: acceleration minus gravity, which is the quantity an otolith senses. At
            rest the acceleration is zero and this reads (0, +g, 0); in free fall the acceleration
            is gravity and this reads zero, which is why falling feels like nothing.
        */
        const MathLib::Vec3 acceleration{(creature.velocity - velocity_before) * (1.0f / dt)};
        const MathLib::Vec3 world_specific_force{acceleration.x, acceleration.y + GRAVITY, acceleration.z};

        // Into the body frame: the inverse of the yaw rotation, which is the yaw rotation by -yaw.
        const Pose inverse_yaw{.position = MathLib::Vec3{}, .yaw = -yaw_after};
        creature.specific_force = worldDirectionFromBody(inverse_yaw, world_specific_force);

        /*
            Truncate to the body's contact budget by discarding the faintest, exactly as the
            descriptor documents — while preserving generation order among the kept, because the
            ABI promises the Grid's own contact order rather than a sort by strength.
        */
        while (creature.contacts.size() > creature.body.max_contact_count) {
            size_t faintest{0u};
            float faintest_magnitude{std::numeric_limits<float>::max()};
            for (size_t index{0u}; index < creature.contacts.size(); ++index) {
                const TglContact& contact{creature.contacts[index]};
                const float magnitude{(contact.impulse[0] * contact.impulse[0]) + (contact.impulse[1] * contact.impulse[1]) + (contact.impulse[2] * contact.impulse[2])};
                if (magnitude < faintest_magnitude) {
                    faintest_magnitude = magnitude;
                    faintest = index;
                }
            }
            creature.contacts.erase(creature.contacts.begin() + static_cast<std::ptrdiff_t>(faintest));
        }
    }

    void Roster::tick(SensesSource& senses_source)
    {
        /*
            The lifecycle's documented order, and the reason there are two loops rather than one.
            Physics advances first for every body, acting on the intent staged last tick; then every
            Program is called and its actions are staged. An action therefore takes effect on the
            next tick for every creature alike, and roster order stays free of meaning.
        */
        for (Creature& creature : m_creatures) {
            stepBody(creature, m_ground);
        }

        // Physics has settled, so the roster is now one consistent tick: whoever is calling is
        // calling for everyone. The source reads that context here, once, rather than
        // per-creature, because a fill sees one listener and a call concerns them all.
        senses_source.beginTick(m_creatures);

        for (Creature& creature : m_creatures) {
            TglSenses senses{};
            senses.tick = m_tick;
            senses.dt_seconds = TICK_SECONDS;

            // What the actuators and the body actually did this tick.
            senses.body_forward_speed = creature.forward_speed;
            senses.body_vertical_speed = creature.velocity.y;
            senses.body_turn_rate = creature.turn_rate;

            senses.specific_force[0] = creature.specific_force.x;
            senses.specific_force[1] = creature.specific_force.y;
            senses.specific_force[2] = creature.specific_force.z;

            senses.angular_velocity[1] = creature.turn_rate;

            if (!creature.contacts.empty()) {
                senses.contacts = creature.contacts.data();
                senses.contact_count = static_cast<uint32_t>(creature.contacts.size());
            }

            // The traced senses: eyes, ears, irradiance.
            senses_source.fill(creature, senses);

            TglActions actions{};
            m_library.vtable().program_tick(creature.program, &senses, &actions);

            sanitiseAndClamp(actions, creature.body);
            creature.staged = actions;
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
