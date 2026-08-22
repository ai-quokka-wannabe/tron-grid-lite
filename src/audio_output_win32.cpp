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
    The spectator's speakers on Windows: WASAPI, shared mode, event-driven, on a thread of its own.

    Shared mode because the spectator is a desktop window beside other desktop windows, and the
    mix format is whatever the endpoint already runs at - the mixer is built at that rate rather
    than resampled to it. Float and 16-bit PCM are both written; anything stranger is refused in
    words at open, which is the only time refusing is cheap.
*/

#include "audio.hpp"

#include <atomic>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

namespace AudioLib
{

    namespace
    {

        constexpr REFERENCE_TIME BUFFER_HUNDRED_NANOSECONDS{20 * 10'000}; // 20 ms of cushion.

        // The two sub-formats this plays, by their published GUIDs: MSVC links them from a
        // library MinGW does not carry, and a value in the source is the same on both.
        constexpr GUID SUBTYPE_PCM{0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
        constexpr GUID SUBTYPE_IEEE_FLOAT{0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

        [[nodiscard]] bool sameGuid(const GUID& a, const GUID& b)
        {
            return std::memcmp(&a, &b, sizeof(GUID)) == 0;
        }

        [[nodiscard]] std::string hresultWords(const char* what, const HRESULT result)
        {
            return std::string{what} + " failed (HRESULT 0x" + std::to_string(static_cast<unsigned long>(result)) + ")";
        }

        template<typename T>
        struct Com {
            T* pointer{nullptr};
            ~Com()
            {
                if (pointer != nullptr) {
                    pointer->Release();
                }
            }
            Com() = default;
            Com(const Com&) = delete;
            Com& operator=(const Com&) = delete;
            Com(Com&&) = delete;
            Com& operator=(Com&&) = delete;
        };

        enum class SampleShape : std::uint8_t { Float32, Pcm16 };

        [[nodiscard]] SampleShape shapeOf(const WAVEFORMATEX& format)
        {
            if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample == 32) {
                return SampleShape::Float32;
            }
            if (format.wFormatTag == WAVE_FORMAT_PCM && format.wBitsPerSample == 16) {
                return SampleShape::Pcm16;
            }
            if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                const auto& extensible{reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format)};
                if (sameGuid(extensible.SubFormat, SUBTYPE_IEEE_FLOAT) && format.wBitsPerSample == 32) {
                    return SampleShape::Float32;
                }
                if (sameGuid(extensible.SubFormat, SUBTYPE_PCM) && format.wBitsPerSample == 16) {
                    return SampleShape::Pcm16;
                }
            }
            throw std::runtime_error{"the audio endpoint's mix format is neither 32-bit float nor 16-bit PCM; the spectator stays mute"};
        }

    }

    struct Output::Impl {
        Mixer& mixer;
        std::atomic<bool> stopping{false};
        std::thread thread;
        std::string refusal;
        std::atomic<bool> ready{false};

        explicit Impl(Mixer& the_mixer) :
            mixer(the_mixer)
        {
        }

        void run()
        {
            // COM, the enumerator, the endpoint, the client, the render client: all on this
            // thread, released in reverse when it ends.
            const HRESULT com{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
            if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
                refusal = hresultWords("CoInitializeEx", com);
                ready = true;
                return;
            }
            try {
                serve();
            } catch (const std::exception& error) {
                refusal = error.what();
            }
            ready = true;
            if (SUCCEEDED(com)) {
                CoUninitialize();
            }
        }

        void serve()
        {
            Com<IMMDeviceEnumerator> enumerator;
            HRESULT result{CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator.pointer))};
            if (FAILED(result)) {
                throw std::runtime_error{hresultWords("MMDeviceEnumerator", result)};
            }
            Com<IMMDevice> device;
            result = enumerator.pointer->GetDefaultAudioEndpoint(eRender, eConsole, &device.pointer);
            if (FAILED(result)) {
                throw std::runtime_error{"no default audio endpoint to play on - the spectator stays mute"};
            }
            Com<IAudioClient> client;
            result = device.pointer->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client.pointer));
            if (FAILED(result)) {
                throw std::runtime_error{hresultWords("IAudioClient", result)};
            }

            WAVEFORMATEX* format{nullptr};
            result = client.pointer->GetMixFormat(&format);
            if (FAILED(result) || format == nullptr) {
                throw std::runtime_error{hresultWords("GetMixFormat", result)};
            }
            const SampleShape shape{shapeOf(*format)};
            const std::uint32_t channels{format->nChannels};
            const std::uint32_t rate{format->nSamplesPerSec};
            if (rate != mixer.sampleRate()) {
                CoTaskMemFree(format);
                throw std::runtime_error{"the audio endpoint runs at " + std::to_string(rate) + " Hz but the mixer was built at " + std::to_string(mixer.sampleRate())};
            }

            const HANDLE event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
            result = client.pointer->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, BUFFER_HUNDRED_NANOSECONDS, 0, format, nullptr);
            CoTaskMemFree(format);
            if (FAILED(result)) {
                CloseHandle(event);
                throw std::runtime_error{hresultWords("IAudioClient::Initialize", result)};
            }
            client.pointer->SetEventHandle(event);

            UINT32 buffer_frames{0u};
            client.pointer->GetBufferSize(&buffer_frames);
            Com<IAudioRenderClient> render;
            result = client.pointer->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render.pointer));
            if (FAILED(result)) {
                CloseHandle(event);
                throw std::runtime_error{hresultWords("IAudioRenderClient", result)};
            }

            std::vector<float> stereo(static_cast<std::size_t>(buffer_frames) * 2u, 0.0f);
            client.pointer->Start();
            ready = true;

            while (!stopping.load()) {
                if (WaitForSingleObject(event, 200) != WAIT_OBJECT_0) {
                    continue;
                }
                UINT32 padding{0u};
                if (FAILED(client.pointer->GetCurrentPadding(&padding)) || padding > buffer_frames) {
                    continue;
                }
                const UINT32 frames{buffer_frames - padding};
                if (frames == 0u) {
                    continue;
                }
                BYTE* data{nullptr};
                if (FAILED(render.pointer->GetBuffer(frames, &data)) || data == nullptr) {
                    continue;
                }
                mixer.render(stereo.data(), frames);
                switch (shape) {
                case SampleShape::Float32: {
                    auto* const samples{reinterpret_cast<float*>(data)};
                    for (UINT32 frame{0u}; frame < frames; ++frame) {
                        for (std::uint32_t channel{0u}; channel < channels; ++channel) {
                            samples[(frame * channels) + channel] = (channel < 2u) ? stereo[(frame * 2u) + channel] : 0.0f;
                        }
                    }
                    break;
                }
                case SampleShape::Pcm16: {
                    auto* const samples{reinterpret_cast<std::int16_t*>(data)};
                    for (UINT32 frame{0u}; frame < frames; ++frame) {
                        for (std::uint32_t channel{0u}; channel < channels; ++channel) {
                            const float value{(channel < 2u) ? stereo[(frame * 2u) + channel] : 0.0f};
                            samples[(frame * channels) + channel] = static_cast<std::int16_t>(value * 32767.0f);
                        }
                    }
                    break;
                }
                }
                render.pointer->ReleaseBuffer(frames, 0);
            }
            client.pointer->Stop();
            CloseHandle(event);
        }
    };

    Output::Output(Mixer& mixer) :
        m_impl(new Impl{mixer})
    {
        m_impl->thread = std::thread{[impl = m_impl]() { impl->run(); }};
        while (!m_impl->ready.load()) {
            std::this_thread::yield();
        }
        if (!m_impl->refusal.empty()) {
            const std::string words{m_impl->refusal};
            m_impl->stopping = true;
            m_impl->thread.join();
            delete m_impl;
            m_impl = nullptr;
            throw std::runtime_error{words};
        }
    }

    Output::~Output()
    {
        if (m_impl != nullptr) {
            m_impl->stopping = true;
            m_impl->thread.join();
            delete m_impl;
        }
    }

    std::uint32_t Output::preferredSampleRate()
    {
        // Ask the endpoint once, on the calling thread, so the mixer can be built to match.
        const HRESULT com{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
        std::uint32_t rate{48'000u};
        {
            Com<IMMDeviceEnumerator> enumerator;
            if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator.pointer)))) {
                Com<IMMDevice> device;
                if (SUCCEEDED(enumerator.pointer->GetDefaultAudioEndpoint(eRender, eConsole, &device.pointer))) {
                    Com<IAudioClient> client;
                    if (SUCCEEDED(device.pointer->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client.pointer)))) {
                        WAVEFORMATEX* format{nullptr};
                        if (SUCCEEDED(client.pointer->GetMixFormat(&format)) && format != nullptr) {
                            rate = format->nSamplesPerSec;
                            CoTaskMemFree(format);
                        }
                    }
                }
            }
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return rate;
    }

} // namespace AudioLib
