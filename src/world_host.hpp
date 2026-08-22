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

#include <chrono>
#include <cstdint>
#include <string>
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

        //! Sends every creature's staged intent for the tick after the last whole one, the previous
        //! intent piggybacked, and flushes. Call after `RosterLib::Roster::tick`.
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

    private:
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
    };

} // namespace WorldHostLib
