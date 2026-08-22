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

#include "world_client.hpp"

#include "roster.hpp"
#include "world_definition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{

    //! The two poses' straight blend, except yaw, which takes the shortest arc across ±pi.
    [[nodiscard]] WorldClientLib::InterpolatedCreature blend(const WorldClientLib::CreatureTrack& track, const float alpha)
    {
        constexpr float TWO_PI{6.2831853071795864769f};

        WorldClientLib::InterpolatedCreature result{};
        result.creature_id = track.newest.creature_id;
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            result.position[axis] = track.previous.position[axis] + (track.newest.position[axis] - track.previous.position[axis]) * alpha;
        }
        const float turn{std::remainder(track.newest.yaw - track.previous.yaw, TWO_PI)};
        result.yaw = track.previous.yaw + turn * alpha;
        result.vocalisation = track.previous.vocalisation + (track.newest.vocalisation - track.previous.vocalisation) * alpha;
        return result;
    }

}

namespace WorldClientLib
{

    std::string statusName(const LnkStatus status)
    {
        switch (status) {
        case LNK_REFUSED:
            return "refused";
        case LNK_HANDSHAKE_TIMED_OUT:
            return "the handshake timed out";
        case LNK_PEER_CLOSED:
            return "the connection closed";
        case LNK_FRAME_REFUSED:
            return "a frame the contract refuses";
        case LNK_GARBLED:
            return "a garbled handshake";
        case LNK_IO:
            return "an i/o error";
        case LNK_BAD_ARGUMENT:
            return "a bad argument";
        case LNK_PANIC:
            return "a fault inside Link";
        default:
            return "status " + std::to_string(status);
        }
    }

    LnkWorldDefinition worldDefinition() noexcept
    {
        return LnkWorldDefinition{
            .floor_cells = GRID_FLOOR_CONFIG.cells,
            .floor_cell_size = GRID_FLOOR_CONFIG.cell_size,
            .floor_height = GRID_FLOOR_CONFIG.height,
            .relief_amplitude = GRID_FLOOR_CONFIG.relief_amplitude,
            .relief_wavelength = GRID_FLOOR_CONFIG.relief_wavelength,
            .relief_octaves = GRID_FLOOR_CONFIG.relief_octaves,
            .relief_terraces = GRID_FLOOR_CONFIG.relief_terraces,
            .relief_seed = GRID_FLOOR_CONFIG.relief_seed,
            .dt_seconds = RosterLib::TICK_SECONDS,
            .body_half_height = RosterLib::BODY_HALF_HEIGHT,
        };
    }

    Client::Client(const std::string& address, const std::chrono::milliseconds handshake_timeout) :
        m_library(LinkLib::Library::besideExecutable())
    {
        LnkStatus status{LNK_PANIC};
        std::array<char, 256> detail{};

        // The world goes into the handshake: a Master Control built from a different floor or
        // tick refuses this Grid at the door, and this Grid refuses such a Master Control's
        // WELCOME - the skew bites both ways, in words naming both fingerprints.
        const LnkWorldDefinition definition{worldDefinition()};
        const std::uint64_t world_fingerprint{m_library.vtable().world_fingerprint(&definition)};

        m_connection = m_library.vtable().connect(address.c_str(), LNK_ROLE_SPECTATOR, world_fingerprint, static_cast<std::uint32_t>(handshake_timeout.count()),
            &m_welcome, &status, detail.data(), static_cast<std::uint32_t>(detail.size()));

        if (m_connection == nullptr) {
            const std::string words{detail.data()};
            if (status == LNK_REFUSED) {
                throw std::runtime_error{"Master Control at " + address + " refused this Grid: " + (words.empty() ? statusName(status) : words)};
            }
            throw std::runtime_error{"No Master Control at " + address + " - is it running? (" + (words.empty() ? statusName(status) : words) + ")"};
        }

        m_tick = m_welcome.current_tick;
    }

    Client::~Client()
    {
        if (m_connection != nullptr) {
            m_library.vtable().close(m_connection);
        }
    }

    void Client::poll()
    {
        LnkMessageView view{};
        std::uint8_t pong_flush_remainder{0u};

        for (;;) {
            const LnkStatus status{m_library.vtable().poll(m_connection, &view)};

            if (status == LNK_NOTHING_YET) {
                return;
            }
            if (status == LNK_PEER_CLOSED) {
                throw std::runtime_error{"Master Control is gone: the connection closed."};
            }
            if (status != LNK_OK) {
                throw std::runtime_error{"The wire refused: " + statusName(status) + "."};
            }

            switch (view.type) {
            case LNK_MSG_TICK_STATE: {
                /*
                        The broadcast is a full snapshot: everything alive is in it, so a
                        creature it does not mention is gone, exactly as if a DEREZ had said so.
                        Snapshot-authoritative removal keeps a lost DEREZ from leaving a ghost.
                    */
                const LnkTickStateHeader& header{view.as.tick_state.header};
                const LnkCreatureState* const rows{view.as.tick_state.states};

                std::unordered_map<std::uint32_t, CreatureTrack> next;
                next.reserve(header.creature_count);
                for (std::uint32_t row = 0u; row < header.creature_count; ++row) {
                    const LnkCreatureState& state{rows[row]};
                    const auto known{m_creatures.find(state.creature_id)};
                    CreatureTrack track{};
                    track.newest = state;
                    track.previous = (known != m_creatures.end()) ? known->second.newest : state;
                    next.emplace(state.creature_id, track);
                }
                m_creatures = std::move(next);
                m_tick = header.tick;
                break;
            }
            case LNK_MSG_EVENT:
                m_events.push_back(view.as.event);
                break;
            case LNK_MSG_DEREZ:
                m_creatures.erase(view.as.derez.creature_id);
                m_tick = std::max(m_tick, view.as.derez.tick);
                break;
            case LNK_MSG_PING:
                /*
                    The keepalive contract's client half: a spectator is legitimately mute - it
                    never sends ACTIONS - so answering PONG is the one proof of life it owes,
                    and a server that hears nothing for LNK_KEEPALIVE_DEAD_MILLIS rightly reaps
                    it. Found the honest way: the first real Master Control reaped this very
                    client in its own integration test.
                */
                m_library.vtable().send_pong(m_connection, view.as.ping.nonce);
                m_library.vtable().flush(m_connection, &pong_flush_remainder);
                break;
            case LNK_MSG_REZ:
                // A body's geometry, relayed for every citizen. The spectator still draws the
                // shared placeholder dart; real bodies land in their own etape, and until then
                // the rows are well-formed and uninteresting.
                break;
            case LNK_MSG_BYE:
                throw std::runtime_error{"Master Control ended the world (BYE)."};
            default:
                // A message a spectator has no use for - a stray PONG, a WELCOME repeated.
                // Ignored rather than fatal: it is well-formed, merely uninteresting.
                break;
            }
        }
    }

    std::vector<InterpolatedCreature> Client::interpolated(const float alpha) const
    {
        const float clamped{std::clamp(alpha, 0.0f, 1.0f)};

        std::vector<InterpolatedCreature> result;
        result.reserve(m_creatures.size());
        for (const auto& [creature_id, track] : m_creatures) {
            static_cast<void>(creature_id);
            result.push_back(blend(track, clamped));
        }
        return result;
    }

    std::vector<LnkEvent> Client::drainEvents()
    {
        return std::exchange(m_events, {});
    }

}
