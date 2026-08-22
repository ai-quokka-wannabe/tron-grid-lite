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
    The spectator's speakers where there are none yet: a thread that drains the mixer at the
    rate a device would, into nothing, so the picture and the log behave alike on every
    platform and a voice never lingers. Linux audio (ALSA or PulseAudio) arrives when somebody
    listens there; this is deliberately not a stub that lies about having played.
*/

#include "audio.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace AudioLib
{

    struct Output::Impl {
        Mixer& mixer;
        std::atomic<bool> stopping{false};
        std::thread thread;

        explicit Impl(Mixer& the_mixer) :
            mixer(the_mixer)
        {
        }
    };

    Output::Output(Mixer& mixer) :
        m_impl(new Impl{mixer})
    {
        m_impl->thread = std::thread{[impl = m_impl]() {
            constexpr std::uint32_t FRAMES{480u};
            std::vector<float> sink(static_cast<std::size_t>(FRAMES) * 2u, 0.0f);
            const std::chrono::microseconds period{static_cast<std::int64_t>(FRAMES) * 1'000'000 / impl->mixer.sampleRate()};
            while (!impl->stopping.load()) {
                impl->mixer.render(sink.data(), FRAMES);
                std::this_thread::sleep_for(period);
            }
        }};
    }

    Output::~Output()
    {
        m_impl->stopping = true;
        m_impl->thread.join();
        delete m_impl;
    }

    std::uint32_t Output::preferredSampleRate()
    {
        return 48'000u;
    }

} // namespace AudioLib
