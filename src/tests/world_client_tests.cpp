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

/*
    The spectator's half of a world, tested with the server's half of the same loaded library:
    the test plays Master Control through Link's own vtable, which is the first C++ code ever to
    call the server surface — and exactly why that surface exists, because a test server written
    by hand in C++ would be the second implementation of the wire this organisation forbids.

    Deviceless throughout: a world's tellings need no GPU to be believed or disbelieved.
*/

#include "../link_library.hpp"
#include "../world_client.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <testing/testing.hpp>

namespace
{

    constexpr std::chrono::milliseconds PATIENCE{5000};

    //! Master Control's part, scripted: a listening vtable and the will to answer one client.
    class RehearsalMasterControl {
    public:
        RehearsalMasterControl() :
            m_library(LinkLib::Library::besideExecutable())
        {
            LnkStatus status{LNK_PANIC};
            m_server = m_library.vtable().listen(0u, &status, nullptr, 0u);
            if ((m_server == nullptr) || (status != LNK_OK)) {
                throw std::runtime_error{"the rehearsal could not listen"};
            }
            m_port = m_library.vtable().server_port(m_server);
        }

        ~RehearsalMasterControl()
        {
            if (m_connection != nullptr) {
                m_library.vtable().close(m_connection);
            }
            if (m_server != nullptr) {
                m_library.vtable().close_server(m_server);
            }
        }

        RehearsalMasterControl(const RehearsalMasterControl&) = delete;
        RehearsalMasterControl& operator=(const RehearsalMasterControl&) = delete;
        RehearsalMasterControl(RehearsalMasterControl&&) = delete;
        RehearsalMasterControl& operator=(RehearsalMasterControl&&) = delete;

        [[nodiscard]] std::string address() const
        {
            return "127.0.0.1:" + std::to_string(m_port);
        }

        //! Waits for the one client, walks its handshake, and welcomes it at the given tick.
        void acceptAndWelcome(const std::uint64_t current_tick)
        {
            const std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + PATIENCE};
            LnkStatus status{LNK_PANIC};
            LnkHello hello{};

            while (m_connection == nullptr) {
                m_connection = m_library.vtable().accept(m_server, 5000u, &hello, &status, nullptr, 0u);
                if (m_connection == nullptr) {
                    if (status != LNK_NOTHING_YET) {
                        throw std::runtime_error{"the rehearsal's accept failed: status " + std::to_string(status)};
                    }
                    if (std::chrono::steady_clock::now() >= deadline) {
                        throw std::runtime_error{"nobody knocked within patience"};
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
            }

            if (hello.role != LNK_ROLE_SPECTATOR) {
                throw std::runtime_error{"the client did not come as a spectator"};
            }

            const LnkWelcome welcome{.current_tick = current_tick, .nominal_dt_seconds = 0.03125f, .client_id = 1u};
            check(m_library.vtable().send_welcome(m_connection, &welcome), "send_welcome");
            flush();
        }

        void tellTick(const std::uint64_t tick, const std::vector<LnkCreatureState>& rows)
        {
            const LnkTickStateHeader header{.tick = tick, .creature_count = static_cast<std::uint32_t>(rows.size()), .reserved0 = {}};
            check(m_library.vtable().send_tick_state(m_connection, &header, rows.empty() ? nullptr : rows.data()), "send_tick_state");
            flush();
        }

        void tellEvent(const LnkEvent& event)
        {
            check(m_library.vtable().send_event(m_connection, &event), "send_event");
            flush();
        }

        void tellDerez(const LnkDerez& derez)
        {
            check(m_library.vtable().send_derez(m_connection, &derez), "send_derez");
            flush();
        }

        //! Ends the world the courteous way: BYE, then the socket closes.
        void endTheWorld()
        {
            m_library.vtable().close(m_connection);
            m_connection = nullptr;
        }

    private:
        static void check(const LnkStatus status, const char* const what)
        {
            if (status != LNK_OK) {
                throw std::runtime_error{std::string{"the rehearsal's "} + what + " answered status " + std::to_string(status)};
            }
        }

        void flush()
        {
            std::uint8_t everything_left{0u};
            check(m_library.vtable().flush(m_connection, &everything_left), "flush");
        }

        LinkLib::Library m_library;
        LnkServer* m_server{nullptr};
        LnkClient* m_connection{nullptr};
        std::uint16_t m_port{0u};
    };

    //! Polls the client until its world reaches the wanted tick, within patience.
    void pollUntilTick(WorldClientLib::Client& client, const std::uint64_t tick)
    {
        const std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + PATIENCE};
        while (client.tick() < tick) {
            client.poll();
            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error{"tick " + std::to_string(tick) + " never arrived"};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    [[nodiscard]] LnkCreatureState creature(const std::uint32_t creature_id, const float x, const float yaw)
    {
        LnkCreatureState state{};
        state.creature_id = creature_id;
        state.position[0] = x;
        state.position[1] = 1.0f;
        state.yaw = yaw;
        return state;
    }

}

TEST_CASE(nobody_listening_is_the_ruled_loud_refusal)
{
    try {
        const WorldClientLib::Client client{"127.0.0.1:1", std::chrono::milliseconds{300}};
        TEST_CHECK(false);
    } catch (const std::runtime_error& error) {
        const std::string message{error.what()};
        TEST_CHECK(message.find("Master Control") != std::string::npos);
        TEST_CHECK(message.find("127.0.0.1:1") != std::string::npos);
        TEST_CHECK(message.find("is it running?") != std::string::npos);
    }
}

TEST_CASE(the_world_is_told_interpolated_and_ended)
{
    RehearsalMasterControl rehearsal{};

    std::thread server{[&rehearsal]() {
        rehearsal.acceptAndWelcome(100u);
    }};
    WorldClientLib::Client client{rehearsal.address(), PATIENCE};
    server.join();

    TEST_CHECK_EQUAL(client.welcome().current_tick, 100u);
    TEST_CHECK_EQUAL(client.tick(), 100u);
    TEST_CHECK_EQUAL(client.creatureCount(), 0u);

    // The first telling: a creature appears; with no previous telling, interpolation holds still.
    rehearsal.tellTick(101u, {creature(7u, 0.0f, 3.0f)});
    pollUntilTick(client, 101u);
    TEST_CHECK_EQUAL(client.creatureCount(), 1u);
    TEST_CHECK_CLOSE(client.interpolated(0.0f).front().position[0], 0.0f, 1e-6f);
    TEST_CHECK_CLOSE(client.interpolated(1.0f).front().position[0], 0.0f, 1e-6f);

    /*
        The second telling moves the creature and swings its yaw from 3.0 to -3.0 radians. The
        long way round is 6 radians; the short way crosses the pi seam and is about 0.283. A
        spectator that lerped naively would show the creature spinning almost a full circle for
        what was really a nudge - the halfway yaw must sit near pi, not near zero.
    */
    rehearsal.tellTick(102u, {creature(7u, 10.0f, -3.0f)});
    pollUntilTick(client, 102u);
    const std::vector<WorldClientLib::InterpolatedCreature> late{client.interpolated(0.0f)};
    const std::vector<WorldClientLib::InterpolatedCreature> fresh{client.interpolated(1.0f)};
    const std::vector<WorldClientLib::InterpolatedCreature> halfway{client.interpolated(0.5f)};
    TEST_CHECK_CLOSE(late.front().position[0], 0.0f, 1e-6f);
    TEST_CHECK_CLOSE(fresh.front().position[0], 10.0f, 1e-6f);
    TEST_CHECK_CLOSE(halfway.front().position[0], 5.0f, 1e-6f);
    TEST_CHECK_CLOSE(std::abs(halfway.front().yaw), 3.1415926f, 1e-3f);

    // An event arrives for the ears and drains exactly once.
    LnkEvent event{};
    event.tick = 102u;
    event.creature_id = 7u;
    event.kind = LNK_EVENT_VOCALISATION;
    event.strength = 0.9f;
    rehearsal.tellEvent(event);
    const std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + PATIENCE};
    std::vector<LnkEvent> heard{};
    while (heard.empty()) {
        client.poll();
        heard = client.drainEvents();
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error{"the event never arrived"};
        }
    }
    TEST_CHECK_EQUAL(heard.size(), 1u);
    TEST_CHECK_EQUAL(heard.front().creature_id, 7u);
    TEST_CHECK(client.drainEvents().empty());

    // A snapshot without the creature removes it: the broadcast is the whole world, so absence
    // is departure even if a DEREZ were lost.
    rehearsal.tellTick(103u, {});
    pollUntilTick(client, 103u);
    TEST_CHECK_EQUAL(client.creatureCount(), 0u);

    // The world ends with a BYE, and a spectator with no world has nothing left to watch.
    rehearsal.endTheWorld();
    try {
        const std::chrono::steady_clock::time_point end_deadline{std::chrono::steady_clock::now() + PATIENCE};
        for (;;) {
            client.poll();
            if (std::chrono::steady_clock::now() >= end_deadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        TEST_CHECK(false);
    } catch (const std::runtime_error& error) {
        const std::string message{error.what()};
        TEST_CHECK((message.find("BYE") != std::string::npos) || (message.find("gone") != std::string::npos));
    }
}

TEST_CASE(a_derez_between_snapshots_removes_the_creature)
{
    RehearsalMasterControl rehearsal{};
    std::thread server{[&rehearsal]() {
        rehearsal.acceptAndWelcome(200u);
    }};
    WorldClientLib::Client client{rehearsal.address(), PATIENCE};
    server.join();

    rehearsal.tellTick(201u, {creature(3u, 1.0f, 0.0f), creature(4u, 2.0f, 0.0f)});
    pollUntilTick(client, 201u);
    TEST_CHECK_EQUAL(client.creatureCount(), 2u);

    LnkDerez derez{};
    derez.tick = 202u;
    derez.creature_id = 3u;
    rehearsal.tellDerez(derez);
    pollUntilTick(client, 202u);
    TEST_CHECK_EQUAL(client.creatureCount(), 1u);
    TEST_CHECK_EQUAL(client.interpolated(1.0f).front().creature_id, 4u);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
