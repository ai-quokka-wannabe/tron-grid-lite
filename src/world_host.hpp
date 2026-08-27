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
#include "roster.hpp"
#include "stage.hpp"
#include "world_client.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/*!
    The creature host's half of the wire: the Program's bodies, embodied in Master Control's
    world.

    The topology's third box (TOPOLOGY.md § The creature host): a headless TronGrid Lite with one
    Program library, dialled into the world as a creature host. It rezzes each creature the
    Program drives — its bounds and the render model the Grid accepted at rez — reads the world's
    telling (`TICK_STATE` for the pose, the owner's letter `PROPRIOCEPTION` for the body's feel),
    hands both to the roster so the minds can tick, and sends every mind's intent back as
    `ACTIONS`, the previous tick's intent piggybacked per the silence rules.

    The host has no clock of its own. It ticks the minds when the world has told a tick whole —
    every own body's letter received after that tick's rows — and never interpolates, predicts or
    extrapolates on the sense path: senses come from authoritative tick-N state only.
*/
namespace WorldHostLib
{

    class Host {
    public:
        /*!
            Loads Link from beside the executable, walks the handshake to `host:port` as a
            creature host, and rezzes every creature of `roster` into the world. Throws
            std::runtime_error with the ruled refusal on failure, exactly as the spectator does.
            \param roster The Program's creatures, already rezzed on the Program side. Borrowed;
                   must outlive the host, which tells it the world and reads its intents.
        */
        Host(const std::string& address, std::chrono::milliseconds handshake_timeout, RosterLib::Roster& roster);
        ~Host();

        Host(const Host&) = delete;
        Host& operator=(const Host&) = delete;
        Host(Host&&) = delete;
        Host& operator=(Host&&) = delete;

        /*!
            Drains the wire: rows into the roster's poses, letters into the roster's bodies, PING
            answered, the relay of other bodies noted. Never blocks. Returns true when a tick has
            been told whole since the last call — the moment the minds may tick.
            \throws std::runtime_error when Master Control is gone, said BYE, or refused a frame.
        */
        [[nodiscard]] bool poll();

        /*!
            Sends every creature's staged intent, the previous intent piggybacked, and flushes.
            Call after `RosterLib::Roster::tick`. Drains the wire first, so the intent is tagged
            for the tick the world will step next even when the world told another tick while the
            mind was thinking; a tick made whole by that drain is handed over by the next poll().
        */
        void act();

        //! The last tick the world told whole.
        [[nodiscard]] uint64_t toldTick() const noexcept
        {
            return m_told_tick;
        }

        [[nodiscard]] const LnkWelcome& welcome() const noexcept
        {
            return m_welcome;
        }

        //! The wire identity of the roster's creature at `index`: unique per connection, because
        //! the client id Master Control hands out is.
        [[nodiscard]] uint32_t wireIdentity(uint32_t index) const noexcept;

        //! Every other creature's body REZ has told, DEREZ has not taken, and the world has placed
        //! at least once - never this host's own. A body still awaiting its first row is not yet
        //! on the stage, because nobody knows where it stands.
        [[nodiscard]] std::unordered_map<std::uint32_t, WorldClientLib::Body> guestBodies() const;

        //! Bumped when the set of guest shapes changes, so the stage and the tracers know to rebuild.
        [[nodiscard]] std::uint64_t guestShapesGeneration() const noexcept
        {
            return m_guest_shapes_generation;
        }

        //! The other creatures as the last whole telling placed them: what the hosted senses meet.
        [[nodiscard]] const std::vector<Stage::GuestTelling>& guests() const noexcept
        {
            return m_guests;
        }

        /*!
            The scratches of the last whole telling: every body's slides this tick - the
            hosted bodies' own included, named by their ABI identity, guests by their wire
            identity - ready for the senses. Replaced whole at each whole tick, so a tick
            nobody scraped in is silent.
        */
        [[nodiscard]] const std::vector<Stage::ScratchTelling>& scratches() const noexcept
        {
            return m_scratches;
        }

    private:
        //! Reads everything the wire holds, never blocking; m_ready says whether a tick is whole.
        void drain();

        void rezAll();
        void tell(const LnkTickStateView& view);
        void feel(const LnkProprioceptionView& view);

        LinkLib::Library m_library;
        LnkClient* m_connection{nullptr};
        LnkWelcome m_welcome{};
        RosterLib::Roster& m_roster;
        //! The intent each creature was last sent, for the piggyback.
        std::vector<TglActions> m_previous;
        //! Per creature: whether its letter for m_telling_tick has arrived.
        std::vector<bool> m_felt;
        uint64_t m_telling_tick{0u};
        uint64_t m_told_tick{0u};
        bool m_ready{false};
        //! Whether a wire identity is one of this host's own creatures.
        [[nodiscard]] bool isOwn(std::uint32_t creature_id) const noexcept;
        std::unordered_map<std::uint32_t, WorldClientLib::Body> m_guest_bodies;
        std::uint64_t m_guest_shapes_generation{0u};
        //! The guests a whole telling has placed at least once: the ones whose shapes may stand.
        std::unordered_set<std::uint32_t> m_placed_guests;
        std::vector<Stage::GuestTelling> m_guests;
        //! The scratch EVENTs of the tick being told, and of the last whole one.
        std::vector<Stage::ScratchTelling> m_scratches_being_told;
        std::vector<Stage::ScratchTelling> m_scratches;
        std::vector<Stage::GuestTelling> m_guests_being_told;
    };

} // namespace WorldHostLib
