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
    Master Control's understudy: a real listening world built entirely from Link's server half,
    standing in until the master-control repository exists.

    Three creatures orbit the stage at 32 Hz — one of them blinking out and back every few seconds
    so snapshot removal can be watched happening, one of them calling so events flow — and every
    spectator that dials in is welcomed and told the world until it hangs up. The world itself is
    scripted arithmetic rather than physics, because what this program exists to exercise is the
    wire and the window, not a tick loop that belongs to another repository.

    A tool to run, not a test to assert: `rehearsal_master_control [port]`, then
    `TronGridLite --window` from another terminal. Stops on Ctrl+C.
*/

#include "../link_library.hpp"

#include <lnk/lnk_protocol.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace
{

    constexpr float DT_SECONDS{0.03125f};
    constexpr std::uint32_t TICKS_PER_SECOND{32u};

    //! The blinker's period, in ticks: four seconds there, four seconds gone.
    constexpr std::uint64_t BLINK_HALF_PERIOD{4u * TICKS_PER_SECOND};

    //! The caller calls this often, for this long.
    constexpr std::uint64_t CALL_PERIOD{2u * TICKS_PER_SECOND};
    constexpr std::uint64_t CALL_LENGTH{8u};

    struct Spectator {
        LnkClient* connection{nullptr};
        std::uint32_t client_id{0u};
    };

    //! One orbiting creature's row at a tick: a circle walked at constant speed, facing along it.
    [[nodiscard]] LnkCreatureState orbiter(const std::uint32_t creature_id, const std::uint64_t tick, const float radius, const float angular_speed, const float phase)
    {
        constexpr float TWO_PI{2.0f * std::numbers::pi_v<float>};

        const float time{static_cast<float>(tick) * DT_SECONDS};
        const float angle{(angular_speed * time) + phase};

        LnkCreatureState state{};
        state.creature_id = creature_id;
        state.position[0] = radius * std::cos(angle);
        state.position[1] = 0.05f;
        state.position[2] = radius * std::sin(angle);

        /*
            Forward is -Z at yaw zero, so facing the tangent of an anticlockwise-in-XZ orbit means
            yaw = pi - angle — and it is wrapped onto ±pi deliberately, because a yaw that walks off
            towards infinity would never cross the seam the spectator's shortest-arc blend exists
            for. The rehearsal's job is to exercise exactly that.
        */
        state.yaw = std::remainder(std::numbers::pi_v<float> - angle, TWO_PI);
        state.velocity[0] = -radius * angular_speed * std::sin(angle);
        state.velocity[2] = radius * angular_speed * std::cos(angle);
        state.yaw_rate = -angular_speed;
        return state;
    }

}

int main(const int argc, char** const argv)
{
    try {
        const std::uint16_t port{(argc > 1) ? static_cast<std::uint16_t>(std::stoi(argv[1])) : static_cast<std::uint16_t>(LNK_DEFAULT_PORT)};

        const LinkLib::Library library{LinkLib::Library::besideExecutable()};
        const LnkClientVTable& vtable{library.vtable()};

        LnkStatus status{LNK_PANIC};
        std::array<char, 256> detail{};
        LnkServer* const server{vtable.listen(port, &status, detail.data(), static_cast<std::uint32_t>(detail.size()))};
        if (server == nullptr) {
            std::cerr << "Could not listen on port " << port << ": " << detail.data() << " (status " << status << ")\n";
            return EXIT_FAILURE;
        }

        std::cout << "Rehearsal Master Control listening on port " << vtable.server_port(server) << ". Dial in with: TronGridLite --window\n";

        std::vector<Spectator> spectators;
        std::uint32_t next_client_id{1u};
        std::uint64_t tick{0u};

        std::chrono::steady_clock::time_point next_tick_time{std::chrono::steady_clock::now()};

        for (;;) {
            // Anyone knocking is welcomed at the current tick. One knock per turn is plenty.
            LnkHello hello{};
            LnkClient* const knocked{vtable.accept(server, 1000u, &hello, &status, detail.data(), static_cast<std::uint32_t>(detail.size()))};
            if (knocked != nullptr) {
                const LnkWelcome welcome{.current_tick = tick, .nominal_dt_seconds = DT_SECONDS, .client_id = next_client_id};
                if (vtable.send_welcome(knocked, &welcome) == LNK_OK) {
                    spectators.push_back(Spectator{.connection = knocked, .client_id = next_client_id});
                    std::cout << "Client " << next_client_id << " joined as role " << static_cast<unsigned>(hello.role) << ".\n";
                    ++next_client_id;
                } else {
                    vtable.close(knocked);
                }
            } else if ((status != LNK_NOTHING_YET) && (status != LNK_OK)) {
                std::cout << "A knock came to nothing: status " << status << ".\n";
            }

            // The world this tick: two steady orbiters, and the blinker when it is in this world.
            std::vector<LnkCreatureState> rows;
            rows.push_back(orbiter(1u, tick, 6.0f, 0.6f, 0.0f));
            rows.push_back(orbiter(2u, tick, 9.0f, -0.35f, 2.1f));

            const bool blinker_now{((tick / BLINK_HALF_PERIOD) % 2u) == 0u};
            if (blinker_now) {
                rows.push_back(orbiter(3u, tick, 3.5f, 1.1f, 4.0f));
            }

            const std::uint64_t call_phase{tick % CALL_PERIOD};
            const bool calling{call_phase < CALL_LENGTH};
            if (calling) {
                rows.front().vocalisation = 0.8f;
            }

            const LnkTickStateHeader header{.tick = tick, .creature_count = static_cast<std::uint32_t>(rows.size()), .reserved0 = {}};

            for (Spectator& spectator : spectators) {
                bool alive{vtable.send_tick_state(spectator.connection, &header, rows.data()) == LNK_OK};

                // A departure is a broadcast: the blinker leaves by DEREZ as well as by absence.
                if (alive && !blinker_now && (tick % BLINK_HALF_PERIOD == 0u)) {
                    const LnkDerez derez{.tick = tick, .creature_id = 3u, .reserved0 = {}};
                    alive = (vtable.send_derez(spectator.connection, &derez) == LNK_OK);
                }

                if (alive && calling && (call_phase == 0u)) {
                    LnkEvent event{};
                    event.tick = tick;
                    event.position[0] = rows.front().position[0];
                    event.position[1] = rows.front().position[1];
                    event.position[2] = rows.front().position[2];
                    event.strength = 0.8f;
                    event.creature_id = 1u;
                    event.kind = LNK_EVENT_VOCALISATION;
                    alive = (vtable.send_event(spectator.connection, &event) == LNK_OK);
                }

                std::uint8_t everything_left{0u};
                alive = alive && (vtable.flush(spectator.connection, &everything_left) == LNK_OK);

                // Drain whatever the spectator says; the only word that matters here is silence.
                LnkMessageView view{};
                while (alive) {
                    const LnkStatus poll_status{vtable.poll(spectator.connection, &view)};
                    if (poll_status == LNK_NOTHING_YET) {
                        break;
                    }
                    if (poll_status != LNK_OK) {
                        alive = false;
                    }
                }

                if (!alive) {
                    std::cout << "Client " << spectator.client_id << " left.\n";
                    vtable.close(spectator.connection);
                    spectator.connection = nullptr;
                }
            }
            std::erase_if(spectators, [](const Spectator& spectator) {
                return spectator.connection == nullptr;
            });

            ++tick;
            next_tick_time += std::chrono::microseconds{31250};
            std::this_thread::sleep_until(next_tick_time);
        }
    } catch (const std::exception& error) {
        std::cerr << "Rehearsal Master Control fell over: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
