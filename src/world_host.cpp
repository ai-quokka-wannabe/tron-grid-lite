/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify it under the terms of
    the GNU General Public License as published by the Free Software Foundation, either version
    3 of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
    without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along with this program.
    If not, see https://www.gnu.org/licenses/.
*/

#include "world_host.hpp"

#include "world_client.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

// The ABI's cap and the wire's are one number, held together here, where the Grid speaks both.
static_assert(TGL_SEGMENTS_MAX == LNK_SEGMENTS_MAX, "TGL_SEGMENTS_MAX and LNK_SEGMENTS_MAX drifted apart");

namespace WorldHostLib
{

    Host::Host(const std::string& address, const std::chrono::milliseconds handshake_timeout, RosterLib::Roster& roster) :
        m_library(LinkLib::Library::besideExecutable()),
        m_roster(roster)
    {
        LnkStatus status{LNK_PANIC};
        std::array<char, 256> detail{};

        // The same world as the spectator's, fingerprinted by the loaded Link: a Master Control
        // built from a different floor or tick refuses this host at the door, in words.
        const LnkWorldDefinition definition{WorldClientLib::worldDefinition()};
        const std::uint64_t world_fingerprint{m_library.vtable().world_fingerprint(&definition)};

        m_connection = m_library.vtable().connect(address.c_str(), LNK_ROLE_CREATURE_HOST, world_fingerprint, static_cast<std::uint32_t>(handshake_timeout.count()),
            &m_welcome, &status, detail.data(), static_cast<std::uint32_t>(detail.size()));

        if (m_connection == nullptr) {
            const std::string words{detail.data()};
            if (status == LNK_REFUSED) {
                throw std::runtime_error{"Master Control at " + address + " refused this host: " + (words.empty() ? WorldClientLib::statusName(status) : words)};
            }
            throw std::runtime_error{"No Master Control at " + address + " - is it running? (" + (words.empty() ? WorldClientLib::statusName(status) : words) + ")"};
        }

        m_told_tick = m_welcome.current_tick;
        m_telling_tick = m_welcome.current_tick;
        m_previous.assign(m_roster.creatures().size(), TglActions{});
        m_felt.assign(m_roster.creatures().size(), false);

        try {
            rezAll();
        } catch (...) {
            // The handshake succeeded, so the connection is real and owed its close before the
            // refusal leaves this frame - a destructor never runs for a constructor that threw.
            m_library.vtable().close(m_connection);
            m_connection = nullptr;
            throw;
        }
    }

    Host::~Host()
    {
        if (m_connection != nullptr) {
            // A leave, not a crash: close sends BYE, and Master Control derezzes what this host
            // owned. The orderly release inside Link lets the farewell land.
            m_library.vtable().close(m_connection);
        }
    }

    std::unordered_map<std::uint32_t, WorldClientLib::Body> Host::guestBodies() const
    {
        std::unordered_map<std::uint32_t, WorldClientLib::Body> placed;
        for (const auto& [creature_id, body] : m_guest_bodies) {
            if (m_placed_guests.contains(creature_id)) {
                placed.emplace(creature_id, body);
            }
        }
        return placed;
    }

    bool Host::isOwn(const std::uint32_t creature_id) const noexcept
    {
        return (creature_id >> 8u) == m_welcome.client_id && (creature_id & 0xFFu) < m_roster.creatures().size();
    }

    std::uint32_t Host::wireIdentity(const std::uint32_t index) const noexcept
    {
        // The client id is unique for the server's life and at most 2^24 hosts deep before this
        // wraps; the low byte is the roster index, whose cap is the snapshot's own.
        return (m_welcome.client_id << 8u) | (index & 0xFFu);
    }

    void Host::rezAll()
    {
        const std::vector<RosterLib::Creature>& creatures{m_roster.creatures()};
        if (creatures.size() > 256u) {
            // The low byte of the wire identity is the roster index; a 257th creature would
            // wear the first's name.
            throw std::runtime_error{"a host may wear at most 256 creatures; this roster has " + std::to_string(creatures.size())};
        }
        for (std::uint32_t index{0u}; index < creatures.size(); ++index) {
            const RosterLib::Creature& creature{creatures[index]};

            /*
                The body as the Program offered it and the Grid accepted it: the ABI's validated
                copy, re-expressed in the wire's own rows. The vertex positions are the same
                floats; the triangles and materials are the same structs under another name -
                TglRenderMaterial and LnkRezMaterial are one shape, which the wire's header
                states and pins.
            */
            std::vector<LnkRezVertex> vertices;
            vertices.reserve(creature.model.vertex_positions.size());
            for (const MathLib::Vec3& position : creature.model.vertex_positions) {
                vertices.push_back(LnkRezVertex{.position = {position.x, position.y, position.z}});
            }
            std::vector<LnkRezTriangle> triangles;
            triangles.reserve(creature.model.triangles.size());
            for (const TglRenderTriangle& triangle : creature.model.triangles) {
                triangles.push_back(LnkRezTriangle{.vertices = {triangle.vertices[0], triangle.vertices[1], triangle.vertices[2]}, .material = triangle.material});
            }
            std::vector<LnkRezMaterial> materials;
            materials.reserve(creature.model.materials.size());
            for (const TglRenderMaterial& material : creature.model.materials) {
                materials.push_back(LnkRezMaterial{.colour = {material.colour[0], material.colour[1], material.colour[2]},
                    .index_of_refraction = material.index_of_refraction,
                    .emission = {material.emission[0], material.emission[1], material.emission[2]},
                    .transmission = material.transmission});
            }

            const LnkRez rez{.creature_id = wireIdentity(index),
                .max_forward_speed = creature.body.max_forward_speed,
                .max_turn_rate = creature.body.max_turn_rate,
                .max_vocalisation_strength = creature.body.max_vocalisation_strength,
                .max_contact_count = creature.body.max_contact_count,
                .vertex_count = static_cast<std::uint32_t>(vertices.size()),
                .triangle_count = static_cast<std::uint32_t>(triangles.size()),
                .material_count = static_cast<std::uint32_t>(materials.size()),
                .segment_count = creature.model.segment_count,
                .segment_spacing = creature.model.segment_spacing};

            const LnkStatus status{m_library.vtable().send_rez(m_connection, &rez, vertices.empty() ? nullptr : vertices.data(),
                triangles.empty() ? nullptr : triangles.data(), materials.empty() ? nullptr : materials.data())};
            if (status != LNK_OK) {
                throw std::runtime_error{"Link refused to stage the rez of creature " + std::to_string(index) + ": " + WorldClientLib::statusName(status)};
            }
        }

        std::uint8_t everything_left{0u};
        const LnkStatus flushed{m_library.vtable().flush(m_connection, &everything_left)};
        if (flushed != LNK_OK) {
            throw std::runtime_error{"The rez could not be sent: " + WorldClientLib::statusName(flushed)};
        }
    }

    bool Host::poll()
    {
        drain();
        // Handed over once: a tick made whole during act()'s drain waits here for this call.
        const bool ready{m_ready};
        m_ready = false;
        return ready;
    }

    void Host::drain()
    {
        std::uint8_t pong_flush_remainder{0u};

        for (;;) {
            LnkMessageView view{};
            const LnkStatus status{m_library.vtable().poll(m_connection, &view)};
            if (status == LNK_NOTHING_YET) {
                break;
            }
            if (status == LNK_PEER_CLOSED) {
                throw std::runtime_error{"Master Control is gone: the connection closed."};
            }
            if (status != LNK_OK) {
                throw std::runtime_error{"The wire to Master Control failed: " + WorldClientLib::statusName(status)};
            }

            switch (view.type) {
            case LNK_MSG_TICK_STATE:
                tell(view.as.tick_state);
                break;
            case LNK_MSG_PROPRIOCEPTION:
                feel(view.as.proprioception);
                break;
            case LNK_MSG_PING:
                m_library.vtable().send_pong(m_connection, view.as.ping.nonce);
                m_library.vtable().flush(m_connection, &pong_flush_remainder);
                break;
            case LNK_MSG_REZ: {
                // Another creature's body, relayed: kept for the stage, so the hosted senses meet
                // it. Our own come back too - the acknowledgement - and are not guests.
                const LnkRezView& told{view.as.rez};
                if (isOwn(told.rez.creature_id)) {
                    break;
                }
                WorldClientLib::Body body{};
                body.rez = told.rez;
                body.vertices.assign(told.vertices, told.vertices + told.rez.vertex_count);
                body.triangles.assign(told.triangles, told.triangles + told.rez.triangle_count);
                body.materials.assign(told.materials, told.materials + told.rez.material_count);
                const auto previous{m_guest_bodies.find(told.rez.creature_id)};
                const bool shape_changes{!body.empty() || ((previous != m_guest_bodies.end()) && !previous->second.empty())};
                m_guest_bodies[told.rez.creature_id] = std::move(body);
                // A shape stands on the stage only once the world has placed it: a body whose
                // first row is still to come would otherwise stand at the origin for a tick.
                if (shape_changes && m_placed_guests.contains(told.rez.creature_id)) {
                    ++m_guest_shapes_generation;
                }
                break;
            }
            case LNK_MSG_DEREZ:
                if (const auto body{m_guest_bodies.find(view.as.derez.creature_id)}; body != m_guest_bodies.end()) {
                    if (!body->second.empty() && m_placed_guests.contains(view.as.derez.creature_id)) {
                        ++m_guest_shapes_generation;
                    }
                    m_guest_bodies.erase(body);
                }
                m_placed_guests.erase(view.as.derez.creature_id);
                break;
            case LNK_MSG_BYE:
                throw std::runtime_error{"Master Control ended the world (BYE)."};
            default:
                // EVENT: well-formed and, for a host whose guests' calls come from the rows,
                // uninteresting.
                break;
            }
        }
    }

    void Host::tell(const LnkTickStateView& view)
    {
        const std::uint64_t tick{view.header.tick};
        if (tick <= m_telling_tick) {
            return; // Never rewind: a stale telling is ignored, per the latest-wins rule.
        }
        m_telling_tick = tick;
        m_ready = false; // A newer telling has begun: whatever was whole is no longer the latest.
        std::fill(m_felt.begin(), m_felt.end(), false);
        m_guests_being_told.clear();

        const std::vector<RosterLib::Creature>& creatures{m_roster.creatures()};
        for (std::uint32_t row{0u}; row < view.header.creature_count; ++row) {
            // SAFETY of the borrow: the rows are the library's own until the next poll, and this
            // read completes inside this call.
            const LnkCreatureState& state{view.states[row]};
            const RosterLib::Pose pose{.position = MathLib::Vec3{state.position[0], state.position[1], state.position[2]}, .yaw = state.yaw};
            // The chain, as the world placed it: the trailing segments' poses, in chain order.
            // The wire refused any row whose count was out of range before this read.
            std::vector<RosterLib::Pose> trail;
            const std::uint32_t trailing{(state.segment_count > 1u) ? (state.segment_count - 1u) : 0u};
            trail.reserve(trailing);
            for (std::uint32_t segment{0u}; segment < trailing; ++segment) {
                const LnkSegmentPose& placed{state.segments[segment]};
                trail.push_back(RosterLib::Pose{.position = MathLib::Vec3{placed.position[0], placed.position[1], placed.position[2]}, .yaw = placed.yaw});
            }
            if (isOwn(state.creature_id)) {
                for (std::uint32_t index{0u}; index < creatures.size(); ++index) {
                    if (state.creature_id == wireIdentity(index)) {
                        // The voice too: what the world sounded, clamped by its law, so the
                        // own ears hear the same call every other ear hears.
                        m_roster.tellPose(index, pose, MathLib::Vec3{state.velocity[0], state.velocity[1], state.velocity[2]}, state.yaw_rate, state.vocalisation);
                        m_roster.tellTrail(index, trail);
                    }
                }
            } else {
                // Everyone else: a guest the hosted senses will meet once this tick is whole.
                m_guests_being_told.push_back(Stage::GuestTelling{.creature_id = state.creature_id,
                    .pose = pose,
                    .velocity = MathLib::Vec3{state.velocity[0], state.velocity[1], state.velocity[2]},
                    .vocalisation = state.vocalisation,
                    .trail = std::move(trail)});
            }
        }
    }

    void Host::feel(const LnkProprioceptionView& view)
    {
        const LnkProprioception& letter{view.proprioception};
        if (letter.tick != m_telling_tick) {
            return; // A letter for another tick than the one being told: never applied out of turn.
        }

        const std::vector<RosterLib::Creature>& creatures{m_roster.creatures()};
        for (std::uint32_t index{0u}; index < creatures.size(); ++index) {
            if (letter.creature_id != wireIdentity(index)) {
                continue;
            }
            // Body frame on the wire, body frame in the ABI: one shape, copied.
            std::vector<TglContact> contacts;
            contacts.reserve(letter.contact_count);
            for (std::uint32_t contact{0u}; contact < letter.contact_count; ++contact) {
                const LnkContact& felt{view.contacts[contact]};
                contacts.push_back(TglContact{.position = {felt.position[0], felt.position[1], felt.position[2]},
                    .impulse = {felt.impulse[0], felt.impulse[1], felt.impulse[2]},
                    .normal = {felt.normal[0], felt.normal[1], felt.normal[2]},
                    .depth = felt.depth,
                    .slip = {felt.slip[0], felt.slip[1], felt.slip[2]}});
            }
            m_roster.tellFeel(index, letter.grounded != 0u, MathLib::Vec3{letter.specific_force[0], letter.specific_force[1], letter.specific_force[2]},
                std::move(contacts));
            m_felt[index] = true;
        }

        if (std::all_of(m_felt.begin(), m_felt.end(), [](const bool felt) {
                return felt;
            })) {
            m_told_tick = m_telling_tick;
            m_guests = m_guests_being_told;
            // A guest the world has now placed for the first time may take the stage.
            for (const Stage::GuestTelling& guest : m_guests) {
                if (m_placed_guests.insert(guest.creature_id).second) {
                    const auto body{m_guest_bodies.find(guest.creature_id)};
                    if ((body != m_guest_bodies.end()) && !body->second.empty()) {
                        ++m_guest_shapes_generation;
                    }
                }
            }
            m_ready = true;
        }
    }

    void Host::act()
    {
        // The world did not wait for the mind. Whatever it told while the Program thought is read
        // first, so the intent is tagged for the tick the world will step next - never for one it
        // has already stepped, which Master Control would refuse as stale, on the record.
        drain();

        const std::vector<RosterLib::Creature>& creatures{m_roster.creatures()};
        for (std::uint32_t index{0u}; index < creatures.size(); ++index) {
            const TglActions& staged{creatures[index].staged};
            const TglActions& previous{m_previous[index]};
            // The tick the world will step next: after the newest telling, whole or not - rows
            // for N+1 already here mean N+1 is stepped, and an intent for it would be stale.
            const LnkActions actions{.tick = m_telling_tick + 1u,
                .creature_id = wireIdentity(index),
                .desired_forward_speed = staged.desired_forward_speed,
                .desired_turn_rate = staged.desired_turn_rate,
                .vocalisation_strength = staged.vocalisation_strength,
                .previous_forward_speed = previous.desired_forward_speed,
                .previous_turn_rate = previous.desired_turn_rate,
                .previous_vocalisation = previous.vocalisation_strength,
                .reserved0 = {}};
            const LnkStatus status{m_library.vtable().send_actions(m_connection, &actions)};
            if (status != LNK_OK) {
                throw std::runtime_error{"Link refused to stage ACTIONS for creature " + std::to_string(index) + ": " + WorldClientLib::statusName(status)};
            }
            m_previous[index] = staged;
        }

        std::uint8_t everything_left{0u};
        const LnkStatus flushed{m_library.vtable().flush(m_connection, &everything_left)};
        if (flushed != LNK_OK) {
            throw std::runtime_error{"ACTIONS could not be sent: " + WorldClientLib::statusName(flushed)};
        }
    }

} // namespace WorldHostLib
