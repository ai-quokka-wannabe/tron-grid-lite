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
        /*
            The servos, for a body that brings a chain: about fifty degrees of swing, what a
            lateral undulator's joints move through, and five newton-metres of torque, a small
            servo's, on a kilogram body. The descriptor precedes the model, so this is the Grid's
            limit for the class; which class the body has is settled once its model is known.
        */
        desc.max_joint_angle = 0.9f;
        desc.max_joint_torque = 5.0f;
        return desc;
    }

    /*!
        Copies a Program's borrowed model into owned storage, validating the whole of it first.

        Accepted entire or refused entire, with the reason. Validation is not politeness: a
        non-finite vertex or a zero-area triangle entering the world's hierarchy poisons a
        traversal that fails somewhere else entirely, on behalf of every creature at once —
        exactly the failure Master Control's sanitise-and-clamp stops in the other direction.
        Actions are sanitised rather than refused because they arrive every tick and a stream
        must degrade gracefully; a model arrives once, at rez, where a loud refusal is cheap
        and a silent repair would ship a body its author never saw.

        \throws std::runtime_error naming the first defect found.
    */
    [[nodiscard]] RosterLib::CreatureModel copyValidatedModel(const TglRenderModel& model)
    {
        RosterLib::CreatureModel out{};

        if (model.triangle_count == 0u) {
            // No visible body. Every other field is ignored, exactly as the ABI documents.
            return out;
        }

        if ((model.vertex_positions == nullptr) || (model.triangles == nullptr) || (model.materials == nullptr) || (model.vertex_count == 0u)
            || (model.material_count == 0u)) {
            throw std::runtime_error{"the model declares triangles without the arrays to build them from"};
        }

        out.vertex_positions.reserve(model.vertex_count);
        for (uint32_t vertex{0u}; vertex < model.vertex_count; ++vertex) {
            const float x{model.vertex_positions[(vertex * 3u) + 0u]};
            const float y{model.vertex_positions[(vertex * 3u) + 1u]};
            const float z{model.vertex_positions[(vertex * 3u) + 2u]};
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                throw std::runtime_error{"vertex " + std::to_string(vertex) + " is not finite"};
            }
            out.vertex_positions.push_back(MathLib::Vec3{x, y, z});
        }

        out.triangles.reserve(model.triangle_count);
        for (uint32_t index{0u}; index < model.triangle_count; ++index) {
            const TglRenderTriangle& triangle{model.triangles[index]};
            for (const uint32_t vertex : triangle.vertices) {
                if (vertex >= model.vertex_count) {
                    throw std::runtime_error{
                        "triangle " + std::to_string(index) + " names vertex " + std::to_string(vertex) + " of a model with " + std::to_string(model.vertex_count)};
                }
            }
            if (triangle.material >= model.material_count) {
                throw std::runtime_error{"triangle " + std::to_string(index) + " names material " + std::to_string(triangle.material) + " of a model with "
                    + std::to_string(model.material_count)};
            }

            const MathLib::Vec3 edge1{out.vertex_positions[triangle.vertices[1]] - out.vertex_positions[triangle.vertices[0]]};
            const MathLib::Vec3 edge2{out.vertex_positions[triangle.vertices[2]] - out.vertex_positions[triangle.vertices[0]]};
            const MathLib::Vec3 cross{edge1.cross(edge2)};
            if (!(cross.dot(cross) > 0.0f)) {
                throw std::runtime_error{"triangle " + std::to_string(index) + " has no area, and a normal cannot be derived from it"};
            }

            out.triangles.push_back(triangle);
        }

        out.materials.reserve(model.material_count);
        for (uint32_t index{0u}; index < model.material_count; ++index) {
            const TglRenderMaterial& material{model.materials[index]};
            for (const float value : {material.colour[0], material.colour[1], material.colour[2], material.index_of_refraction, material.emission[0],
                     material.emission[1], material.emission[2], material.transmission}) {
                if (!std::isfinite(value)) {
                    throw std::runtime_error{"material " + std::to_string(index) + " is not finite"};
                }
            }
            if (!(material.index_of_refraction > 0.0f)) {
                throw std::runtime_error{"material " + std::to_string(index) + " has a non-positive index of refraction, which Snell's law cannot bend"};
            }
            if ((material.transmission < 0.0f) || (material.transmission > 1.0f)) {
                throw std::runtime_error{"material " + std::to_string(index) + " transmits outside zero to one, which is not a fraction"};
            }
            out.materials.push_back(material);
        }

        // The chain: a count in range, a spacing that fits it - a single body has none, a chain
        // has a positive finite one. Refused by name, like every other lie a model can tell.
        if ((model.segment_count == 0u) || (model.segment_count > TGL_SEGMENTS_MAX)) {
            throw std::runtime_error{"the model declares " + std::to_string(model.segment_count) + " segments; a chain has one to " + std::to_string(TGL_SEGMENTS_MAX)};
        }
        if (!std::isfinite(model.segment_spacing)) {
            throw std::runtime_error{"the model's segment spacing is not finite"};
        }
        if ((model.segment_count == 1u) && (model.segment_spacing != 0.0f)) {
            throw std::runtime_error{"a single body declares a segment spacing, which nothing could be spaced by"};
        }
        if ((model.segment_count > 1u) && !(model.segment_spacing > 0.0f)) {
            throw std::runtime_error{"a chain of " + std::to_string(model.segment_count) + " declares no spacing between its segments"};
        }
        out.segment_count = model.segment_count;
        out.segment_spacing = model.segment_spacing;

        return out;
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
        try {
            for (uint32_t index{0u}; index < creature_count; ++index) {
                const TglCreatureDesc desc{firstBody(index)};

                // Zeroed by the Grid before the call, exactly as actions are: a Program that offers
                // no body must not inherit whatever was on the stack and be judged on it.
                TglRenderModel model{};

                TglProgram* const program{m_library.vtable().program_rez(&desc, &model)};
                if (program == nullptr) {
                    throw std::runtime_error{"Program \"" + m_library.identifier() + "\" refused to rez creature " + std::to_string(index) + "."};
                }

                Creature creature;
                creature.program = program;
                creature.body = desc;

                try {
                    creature.model = copyValidatedModel(model);
                    // Which actuators this body has follows from the body it brought: a chain
                    // is a row of servos at its pivots and has no velocity actuator; a body of
                    // one segment has the velocity actuators and no servos. A bound of zero is
                    // no such actuator, and that is what the world is told at REZ.
                    if (creature.model.segment_count > 1u) {
                        creature.body.max_forward_speed = 0.0f;
                        creature.body.max_turn_rate = 0.0f;
                    } else {
                        creature.body.max_joint_angle = 0.0f;
                        creature.body.max_joint_torque = 0.0f;
                    }
                } catch (const std::exception& defect) {
                    // The rez succeeded, so the handle is real and owed its derez before the refusal
                    // leaves this frame — a Program is entitled to unwind in program_derez what it
                    // built in program_rez.
                    m_library.vtable().program_derez(program);
                    throw std::runtime_error{
                        "Program \"" + m_library.identifier() + "\" offered creature " + std::to_string(index) + " a model the Grid refuses: " + defect.what() + "."};
                }

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
        } catch (...) {
            // A destructor never runs for a constructor that threw, and the library's own
            // destructor would shut the Program with creatures still rezzed - which the header
            // forbids: a Program touches its state in program_derez, never after library_shutdown.
            // Every creature this constructor rezzed is derezzed here, in reverse, before the
            // refusal leaves.
            for (auto creature{m_creatures.rbegin()}; creature != m_creatures.rend(); ++creature) {
                m_library.vtable().program_derez(creature->program);
            }
            m_creatures.clear();
            throw;
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
        /*
            The mind's half of the lifecycle, which is all that lives here since the physics
            followed its owner to Master Control: the world's telling advances the bodies, and
            this loop advances the minds - senses in, program called, intent staged. The voice
            actuator is applied from the staged intent so every ear still hears one consistent
            tick; over the wire it is the server's physics that applies it, from the very same
            staged value this host sends.
        */

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
            // The servos' own readings, as the world's letter said them.
            for (std::size_t joint{0u}; joint < creature.joint_angles.size(); ++joint) {
                senses.joint_angles[joint] = creature.joint_angles[joint];
                senses.joint_torques[joint] = creature.joint_torques[joint];
            }

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

            // Staged raw: the host's clamp was convenience and the server's is the law -
            // Master Control sanitises every intent before its physics ever sees it.
            creature.staged = actions;
        }

        ++m_tick;
    }

    void Roster::tellPose(const uint32_t index, const Pose& pose, const MathLib::Vec3& velocity, const float yaw_rate, const float vocalisation)
    {
        Creature& creature{m_creatures.at(index)};
        creature.pose = pose;
        creature.velocity = velocity;
        creature.turn_rate = yaw_rate;
        creature.vocalisation = vocalisation;
        const MathLib::Vec3 forward{forwardFor(pose.yaw)};
        creature.forward_speed = (velocity.x * forward.x) + (velocity.z * forward.z);
    }

    void Roster::tellTrail(const uint32_t index, std::vector<Pose> trail)
    {
        m_creatures.at(index).trail = std::move(trail);
    }

    void Roster::tellFeel(const uint32_t index, const bool grounded, const MathLib::Vec3& specific_force, const std::array<float, TGL_SEGMENTS_MAX - 1u>& joint_angles,
        const std::array<float, TGL_SEGMENTS_MAX - 1u>& joint_torques, std::vector<TglContact> contacts)
    {
        Creature& creature{m_creatures.at(index)};
        creature.grounded = grounded;
        creature.specific_force = specific_force;
        creature.joint_angles = joint_angles;
        creature.joint_torques = joint_torques;
        creature.contacts = std::move(contacts);
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
