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

#pragma once

#include "link_library.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/*!
    The client's living copy of Master Control's world: what TICK_STATE said, remembered one
    telling deep so the picture can interpolate between the two newest ticks rather than jump,
    with no prediction ever. One telling is the depth localhost TCP actually needs — delivery
    jitter here is microseconds against a 31 ms tick — and the blueprint's two-to-three-tick
    ring buffer (TOPOLOGY.md § The spectator) deepens this exact seam the day the UDP trigger
    fires. Clients own perception; the world stays the server's.
*/
namespace WorldClientLib
{

    /*!
        A creature's body as REZ told it: the bounds and the render model the host offered and
        Master Control relayed, verbatim. Kept by creature so the stage can build the real shape
        the first telling stands it in; empty rows are a bodiless creature, which wears the
        placeholder.
    */
    struct Body {
        LnkRez rez{};
        std::vector<LnkRezVertex> vertices;
        std::vector<LnkRezTriangle> triangles;
        std::vector<LnkRezMaterial> materials;

        [[nodiscard]] bool empty() const noexcept
        {
            return triangles.empty();
        }
    };

    //! One creature as the world last told it, with the previous telling kept for interpolation.
    struct CreatureTrack {
        LnkCreatureState newest{};
        LnkCreatureState previous{};
    };

    //! A creature's pose a fraction of the way from the previous telling to the newest.
    struct InterpolatedCreature {
        std::uint32_t creature_id{0u};
        float position[3]{};
        float yaw{0.0f};
        //! Radians about the body's right hand, positive nose up, blended like the yaw.
        float pitch{0.0f};
        float vocalisation{0.0f};
        //! The chain: segments, the head counted, and the trailing segments' poses blended the
        //! same way, `segment_count - 1` of them meaningful.
        std::uint32_t segment_count{1u};
        LnkSegmentPose segments[LNK_SEGMENTS_MAX - 1u]{};
    };

    /*!
        The world this Grid is built from, in Link's words: the floor (`GRID_FLOOR_CONFIG`), the
        tick (`RosterLib::TICK_SECONDS`) and the body's half height — the fields Master Control
        must agree on before a creature's position means the same thing at both ends. One
        function, so the client and the tests describe the same world; the fingerprint over it
        is the loaded library's to compute, never this side's.
    */
    [[nodiscard]] LnkWorldDefinition worldDefinition() noexcept;

    //! A Link status in words, for the refusals this side throws.
    [[nodiscard]] std::string statusName(LnkStatus status);

    class Client {
    public:
        /*!
            Loads Link from beside the executable — the residence rule — and walks the whole
            handshake to the given `host:port` as a spectator. Throws std::runtime_error with
            the ruled refusal on failure: a server that refused quotes its words verbatim, and
            an absent one earns "no Master Control at the address — is it running?". There is
            deliberately no silent fallback: a fallen server must never look like an empty
            world.
        */
        Client(const std::string& address, std::chrono::milliseconds handshake_timeout);

        /*!
            Clu's half: opens a Disk instead of dialling a world. The same client, the same
            poll, the same picture - a replay viewer is a spectator whose socket is a file. The
            Disk's header is judged as a handshake is (another contract, another world: refused
            in words), and its frames are paced by the recorded dt against the wall clock, so a
            Disk plays back at the speed the world ran: a tick is applied only when its time has
            come. At the end of the Disk the world stands still rather than ending - see `ended`.
        */
        explicit Client(const std::filesystem::path& disk);

        //! Says BYE and closes. A courtesy, not a contract: a power cut sends no BYE either.
        ~Client();

        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;
        Client(Client&&) = delete;
        Client& operator=(Client&&) = delete;

        /*!
            Drains everything the socket holds whole: TICK_STATE replaces the world (the
            broadcast is a full snapshot, so a creature absent from it is gone), EVENT queues
            for the ears, DEREZ removes. Never blocks. Throws when the world is over — BYE, a
            closed connection, or a frame the contract refuses — because a spectator with no
            world has nothing left to watch.
        */
        void poll();

        [[nodiscard]] const LnkWelcome& welcome() const noexcept
        {
            return m_welcome;
        }

        //! True once a replayed Disk has been played to its end: the last telling stands.
        [[nodiscard]] bool ended() const noexcept
        {
            return m_ended;
        }

        //! True when this client replays a Disk rather than watching a live world.
        [[nodiscard]] bool replaying() const noexcept
        {
            return m_replay.has_value();
        }

        //! The newest tick the world has told; the WELCOME's tick until the first TICK_STATE.
        [[nodiscard]] std::uint64_t tick() const noexcept
        {
            return m_tick;
        }

        [[nodiscard]] std::size_t creatureCount() const noexcept
        {
            return m_creatures.size();
        }

        /*!
            Every creature's pose, `alpha` of the way from the previous telling to the newest —
            0 draws the world one tick late, 1 draws it as freshly as it is known. Yaw takes the
            shortest arc, so a creature crossing the ±pi seam turns a few degrees rather than
            almost a full circle.
        */
        [[nodiscard]] std::vector<InterpolatedCreature> interpolated(float alpha) const;

        //! Everything EVENT delivered since the last drain, oldest first.
        [[nodiscard]] std::vector<LnkEvent> drainEvents();

        //! The newest row for a creature, or null for one the world does not hold.
        [[nodiscard]] const LnkCreatureState* newest(std::uint32_t creature_id) const noexcept
        {
            const auto known{m_creatures.find(creature_id)};
            return (known != m_creatures.end()) ? &known->second.newest : nullptr;
        }

        //! Every body REZ has told and DEREZ has not yet taken, by creature.
        [[nodiscard]] const std::unordered_map<std::uint32_t, Body>& bodies() const noexcept
        {
            return m_bodies;
        }

        //! Bumped whenever the set of shapes changes - a REZ with rows, or a DEREZ of one - so a
        //! stage can tell a world whose geometry moved from one whose placements did.
        [[nodiscard]] std::uint64_t bodiesGeneration() const noexcept
        {
            return m_bodies_generation;
        }

    private:
        LinkLib::Library m_library;
        LnkClient* m_connection{nullptr};
        LnkWelcome m_welcome{};
        std::uint64_t m_tick{0u};
        std::unordered_map<std::uint32_t, CreatureTrack> m_creatures;
        std::vector<LnkEvent> m_events;
        std::unordered_map<std::uint32_t, Body> m_bodies;
        std::uint64_t m_bodies_generation{0u};

        //! A telling read ahead of its time, held whole until the clock reaches its tick.
        struct PendingTelling {
            LnkTickStateHeader header{};
            std::vector<LnkCreatureState> rows;
        };
        //! The pacing of a replay: when playback began, and the Disk's first tick.
        struct Replay {
            std::chrono::steady_clock::time_point began{};
            std::uint64_t first_tick{0u};
            std::optional<PendingTelling> pending;
        };
        std::optional<Replay> m_replay;
        bool m_ended{false};

        //! One message applied to the picture; shared by the live and the replayed paths.
        void apply(const LnkMessageView& view);
        void applyTelling(const LnkTickStateHeader& header, const LnkCreatureState* rows);
    };

}
