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

/*!
    The creature host's half of a world, tested against the server's half of the same loaded
    Link: a rehearsal Master Control accepts the host, hears its REZ, tells a tick and the
    owner's letter, and hears the mind's ACTIONS back. Deviceless throughout - the roster ticks
    with no traced senses - which is exactly the part the wire host owns.
*/

#include "../roster.hpp"
#include "../world_client.hpp"
#include "../world_host.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <testing/testing.hpp>

namespace
{

    constexpr std::chrono::milliseconds PATIENCE{5000};

    [[nodiscard]] std::filesystem::path fixtureDirectory()
    {
        return std::filesystem::path{TGL_TEST_PROGRAM_DIR};
    }

    [[nodiscard]] RosterLib::GroundFunction flatGround()
    {
        return [](float, float) { return 0.0f; };
    }

    //! Master Control's part, scripted: it listens as the host's own world, and keeps what it hears.
    class RehearsalMasterControl {
    public:
        RehearsalMasterControl() :
            m_library(LinkLib::Library::besideExecutable())
        {
            const LnkWorldDefinition definition{WorldClientLib::worldDefinition()};
            m_world_fingerprint = m_library.vtable().world_fingerprint(&definition);
            LnkStatus status{LNK_PANIC};
            m_server = m_library.vtable().listen(0u, m_world_fingerprint, &status, nullptr, 0u);
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

        //! Accept the host, welcome it at `current_tick` as client `client_id`.
        void acceptAndWelcome(const std::uint64_t current_tick, const std::uint32_t client_id)
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
            if (hello.role != LNK_ROLE_CREATURE_HOST) {
                throw std::runtime_error{"the client did not come as a creature host"};
            }
            const LnkWelcome welcome{.current_tick = current_tick, .nominal_dt_seconds = 0.03125f, .client_id = client_id, .world_fingerprint = m_world_fingerprint};
            check(m_library.vtable().send_welcome(m_connection, &welcome), "send_welcome");
            flush();
        }

        //! Poll until a message of `type` arrives, answering PINGs, keeping what it says.
        LnkMessageView awaitMessage(const std::uint8_t type)
        {
            const std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + PATIENCE};
            for (;;) {
                LnkMessageView view{};
                const LnkStatus status{m_library.vtable().poll(m_connection, &view)};
                if (status == LNK_OK) {
                    if (view.type == type) {
                        return view;
                    }
                    if (view.type == LNK_MSG_PING) {
                        check(m_library.vtable().send_pong(m_connection, view.as.ping.nonce), "send_pong");
                        flush();
                    }
                } else if (status != LNK_NOTHING_YET) {
                    throw std::runtime_error{"the rehearsal's poll failed: status " + std::to_string(status)};
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    throw std::runtime_error{"message type " + std::to_string(type) + " never arrived"};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }

        //! Tell one tick: one row for the creature, then its letter, in one flush.
        void tellTick(const std::uint64_t tick, const std::uint32_t creature_id, const float z, const float yaw, const bool grounded, const bool with_letter = true)
        {
            const LnkTickStateHeader header{.tick = tick, .creature_count = 1u, .reserved0 = {}};
            const LnkCreatureState row{.creature_id = creature_id, .position = {1.0f, 0.05f, z}, .yaw = yaw, .velocity = {0.0f, 0.0f, -0.5f}, .yaw_rate = 0.1f, .vocalisation = 0.0f};
            check(m_library.vtable().send_tick_state(m_connection, &header, &row), "send_tick_state");
            if (with_letter) {
                const std::array<LnkContact, 1> contacts{LnkContact{.position = {0.0f, -0.05f, 0.0f},
                    .impulse = {0.0f, 0.3f, 0.0f},
                    .normal = {0.0f, 1.0f, 0.0f},
                    .depth = 0.004f,
                    .slip = {0.0f, 0.0f, -0.5f}}};
                const LnkProprioception letter{.tick = tick,
                    .creature_id = creature_id,
                    .grounded = static_cast<std::uint8_t>(grounded ? 1u : 0u),
                    .reserved0 = {},
                    .specific_force = {0.0f, 9.81f, 0.0f},
                    .contact_count = grounded ? 1u : 0u};
                check(m_library.vtable().send_proprioception(m_connection, &letter, grounded ? contacts.data() : nullptr), "send_proprioception");
            }
            flush();
        }

        //! A letter on its own, for whatever tick it claims - the stray a host must not obey.
        void tellLetter(const std::uint64_t tick, const std::uint32_t creature_id, const bool grounded)
        {
            const LnkProprioception letter{
                .tick = tick, .creature_id = creature_id, .grounded = static_cast<std::uint8_t>(grounded ? 1u : 0u), .reserved0 = {}, .specific_force = {0.0f, 9.81f, 0.0f}, .contact_count = 0u};
            check(m_library.vtable().send_proprioception(m_connection, &letter, nullptr), "send_proprioception");
            flush();
        }

    private:
        void check(const LnkStatus status, const char* what)
        {
            if (status != LNK_OK) {
                throw std::runtime_error{std::string{what} + " answered status " + std::to_string(status)};
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
        std::uint64_t m_world_fingerprint{0u};
    };

    [[nodiscard]] bool near(const float a, const float b, const float tolerance = 1e-5f)
    {
        return std::fabs(a - b) <= tolerance;
    }

}

TEST_CASE(nobody_listening_is_the_ruled_loud_refusal)
{
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 1u, flatGround()};
    try {
        const WorldHostLib::Host host{"127.0.0.1:1", std::chrono::milliseconds{300}, roster};
        TEST_CHECK(false);
    } catch (const std::runtime_error& error) {
        const std::string message{error.what()};
        TEST_CHECK(message.find("No Master Control") != std::string::npos);
        TEST_CHECK(message.find("is it running?") != std::string::npos);
    }
}

TEST_CASE(the_host_rezzes_its_bodies_reads_the_telling_and_sends_the_minds_intents)
{
    RehearsalMasterControl rehearsal{};
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 1u, flatGround()};

    std::thread server{[&rehearsal]() { rehearsal.acceptAndWelcome(100u, 9u); }};
    WorldHostLib::Host host{rehearsal.address(), PATIENCE, roster};
    server.join();

    // Identity: the client id's, so two hosts in one world can never collide.
    TEST_CHECK_EQUAL(host.wireIdentity(0u), (9u << 8u) | 0u);
    TEST_CHECK_EQUAL(host.toldTick(), 100u);

    // The REZ carries the Program's bounds; tgl_driver_steady offers no model, so it is bodiless.
    const LnkMessageView rez{rehearsal.awaitMessage(LNK_MSG_REZ)};
    TEST_CHECK_EQUAL(rez.as.rez.rez.creature_id, host.wireIdentity(0u));
    TEST_CHECK(near(rez.as.rez.rez.max_forward_speed, roster.creatures().front().body.max_forward_speed));
    TEST_CHECK_EQUAL(rez.as.rez.rez.max_contact_count, roster.creatures().front().body.max_contact_count);
    TEST_CHECK_EQUAL(rez.as.rez.rez.vertex_count, 0u);

    // A tick without its letter is not whole: the minds must not tick on half a telling.
    rehearsal.tellTick(101u, host.wireIdentity(0u), 5.0f, 0.0f, true, false);
    const std::chrono::steady_clock::time_point settle{std::chrono::steady_clock::now() + std::chrono::milliseconds{100}};
    bool ready{false};
    while (std::chrono::steady_clock::now() < settle) {
        ready = ready || host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    TEST_CHECK(!ready);
    TEST_CHECK_EQUAL(host.toldTick(), 100u);
    // The pose, though, was told: rows land as they arrive.
    TEST_CHECK(near(roster.creatures().front().pose.position.z, 5.0f));

    // The whole telling: rows, then the letter. Now the minds may tick.
    rehearsal.tellTick(102u, host.wireIdentity(0u), 4.5f, 0.25f, true);
    const std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + PATIENCE};
    while (!host.poll()) {
        TEST_CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    TEST_CHECK_EQUAL(host.toldTick(), 102u);
    const RosterLib::Creature& creature{roster.creatures().front()};
    TEST_CHECK(near(creature.pose.position.z, 4.5f));
    TEST_CHECK(near(creature.pose.yaw, 0.25f));
    TEST_CHECK(creature.grounded);
    TEST_CHECK_EQUAL(creature.contacts.size(), static_cast<std::size_t>(1u));
    // The contact arrives whole: the face, the depth and the slip the letter carried.
    TEST_CHECK(near(creature.contacts.front().normal[1], 1.0f));
    TEST_CHECK(near(creature.contacts.front().depth, 0.004f));
    TEST_CHECK(near(creature.contacts.front().slip[2], -0.5f));
    TEST_CHECK(near(creature.specific_force.y, 9.81f));
    TEST_CHECK(near(creature.turn_rate, 0.1f));
    // Forward speed is the velocity along the facing: -0.5 m/s along -Z at yaw 0.25 projects to
    // 0.5 cos(0.25) forward.
    TEST_CHECK(near(creature.forward_speed, 0.5f * std::cos(0.25f)));

    // The mind ticks with no traced senses, and its intent goes back tagged for the next tick.
    RosterLib::NullSensesSource null_senses;
    roster.tick(null_senses);
    host.act();
    const LnkMessageView first{rehearsal.awaitMessage(LNK_MSG_ACTIONS)};
    TEST_CHECK_EQUAL(first.as.actions.tick, 103u);
    TEST_CHECK_EQUAL(first.as.actions.creature_id, host.wireIdentity(0u));
    TEST_CHECK(near(first.as.actions.desired_forward_speed, 0.5f));
    TEST_CHECK(near(first.as.actions.previous_forward_speed, 0.0f));

    // The next tick: the previous intent rides along, per the silence rules.
    rehearsal.tellTick(103u, host.wireIdentity(0u), 4.0f, 0.25f, true);
    while (!host.poll()) {
        TEST_CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    roster.tick(null_senses);
    host.act();
    const LnkMessageView second{rehearsal.awaitMessage(LNK_MSG_ACTIONS)};
    TEST_CHECK_EQUAL(second.as.actions.tick, 104u);
    TEST_CHECK(near(second.as.actions.previous_forward_speed, 0.5f));

    // A slow mind: the world told tick 104 whole while the Program was still thinking about 103.
    // The intent is tagged for the tick the world will step next - 105, not 104 - so it is never
    // refused stale; and the tick told meanwhile is not lost: the very next poll hands it over.
    roster.tick(null_senses);
    rehearsal.tellTick(104u, host.wireIdentity(0u), 3.75f, 0.25f, true);
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    host.act();
    const LnkMessageView late{rehearsal.awaitMessage(LNK_MSG_ACTIONS)};
    TEST_CHECK_EQUAL(late.as.actions.tick, 105u);
    TEST_CHECK_EQUAL(host.toldTick(), 104u);
    TEST_CHECK(host.poll());
    TEST_CHECK(near(roster.creatures().front().pose.position.z, 3.75f));
    roster.tick(null_senses);
    host.act();
    TEST_CHECK_EQUAL(rehearsal.awaitMessage(LNK_MSG_ACTIONS).as.actions.tick, 105u);

    // A letter for a tick not being told is never applied out of turn: a stray claiming tick
    // 999 says the feet left the floor, and the body told at 104 keeps them.
    rehearsal.tellLetter(999u, host.wireIdentity(0u), false);
    const std::chrono::steady_clock::time_point stray_settle{std::chrono::steady_clock::now() + std::chrono::milliseconds{100}};
    while (std::chrono::steady_clock::now() < stray_settle) {
        TEST_CHECK(!host.poll());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    TEST_CHECK(roster.creatures().front().grounded);
    TEST_CHECK_EQUAL(host.toldTick(), 104u);

    // And a whole telling with the feet off the floor lands as told.
    rehearsal.tellTick(105u, host.wireIdentity(0u), 3.5f, 0.25f, false);
    while (!host.poll()) {
        TEST_CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    TEST_CHECK(!roster.creatures().front().grounded);
    TEST_CHECK(roster.creatures().front().contacts.empty());
}

TEST_CASE(a_master_control_from_a_different_world_refuses_the_host)
{
    // The rehearsal listens as another world by lying to Link about its fingerprint.
    class OtherWorld {
    public:
        OtherWorld() :
            m_library(LinkLib::Library::besideExecutable())
        {
            LnkStatus status{LNK_PANIC};
            m_server = m_library.vtable().listen(0u, 0xBAD5EEDu, &status, nullptr, 0u);
            m_port = m_library.vtable().server_port(m_server);
        }
        ~OtherWorld()
        {
            m_library.vtable().close_server(m_server);
        }
        OtherWorld(const OtherWorld&) = delete;
        OtherWorld& operator=(const OtherWorld&) = delete;
        OtherWorld(OtherWorld&&) = delete;
        OtherWorld& operator=(OtherWorld&&) = delete;
        [[nodiscard]] std::string address() const
        {
            return "127.0.0.1:" + std::to_string(m_port);
        }
        void refuseOne()
        {
            const std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + PATIENCE};
            LnkStatus status{LNK_PANIC};
            LnkHello hello{};
            for (;;) {
                LnkClient* const knock{m_library.vtable().accept(m_server, 5000u, &hello, &status, nullptr, 0u)};
                if (knock != nullptr) {
                    m_library.vtable().close(knock);
                    throw std::runtime_error{"a host from another world was let in"};
                }
                if (status == LNK_REFUSED) {
                    return;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    throw std::runtime_error{"nobody knocked"};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }

    private:
        LinkLib::Library m_library;
        LnkServer* m_server{nullptr};
        std::uint16_t m_port{0u};
    } other_world{};

    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 1u, flatGround()};
    std::thread server{[&other_world]() { other_world.refuseOne(); }};
    try {
        const WorldHostLib::Host host{other_world.address(), PATIENCE, roster};
        TEST_CHECK(false);
    } catch (const std::runtime_error& error) {
        const std::string message{error.what()};
        TEST_CHECK(message.find("refused this host") != std::string::npos);
        TEST_CHECK(message.find("different world") != std::string::npos);
    }
    server.join();
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
