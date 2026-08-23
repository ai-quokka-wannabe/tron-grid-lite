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

#include "audio.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace AudioLib
{

    namespace
    {

        constexpr float PING_SECONDS{0.45f};
        constexpr float PING_DECAY_SECONDS{0.12f}; //!< The envelope's time constant.
        constexpr float PING_GAIN{0.35f};
        constexpr float SCRATCH_SECONDS{0.06f};
        constexpr float SCRATCH_DECAY_SECONDS{0.018f};
        constexpr float SCRATCH_GAIN{0.5f};
        constexpr float SCRATCH_CUTOFF_HZ{1800.0f};
        //! Distance at which a sound is half as loud as at the ear: a room-sized world.
        constexpr float HALF_LOUDNESS_METRES{4.0f};
        //! The most voices at once; beyond it the oldest retire early, which a burst of
        //! footsteps from a crowd will reach and nobody will hear.
        constexpr std::size_t MAX_VOICES{64u};

    }

    float pitchOf(const std::uint32_t creature_id) noexcept
    {
        // A4 and the eleven semitones above it, by identity: an octave of creatures.
        return 440.0f * std::pow(2.0f, static_cast<float>(creature_id % 12u) / 12.0f);
    }

    Mixer::Mixer(const std::uint32_t sample_rate) :
        m_sample_rate(sample_rate)
    {
        m_voices.reserve(MAX_VOICES);
    }

    void Mixer::setListener(const Listener& listener)
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_listener = listener;
    }

    void Mixer::sound(const Sound& sound)
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (m_voices.size() >= MAX_VOICES) {
            m_voices.erase(m_voices.begin());
        }
        m_voices.push_back(voiceFor(sound, m_listener));
    }

    Mixer::Voice Mixer::voiceFor(const Sound& sound, const Listener& listener) const
    {
        /*
            Placement: the line from the source to the ear. Equal-power pan from its component
            along the listener's right, attenuation by distance. Doppler from the source's speed
            along that line - approaching raises the pitch, receding lowers it - which is the
            classic f' = f * c / (c - v_toward); the listener's own motion is the camera's, a
            fly-by that has no physics, and is deliberately left out.
        */
        const MathLib::Vec3 offset{sound.position - listener.position};
        const float distance{offset.length()};
        const MathLib::Vec3 direction{distance > 1e-4f ? offset * (1.0f / distance) : listener.forward};

        const float side{std::clamp(direction.dot(listener.right), -1.0f, 1.0f)};
        const float pan{(side + 1.0f) * 0.5f}; // 0 left, 1 right
        const float left{std::cos(pan * std::numbers::pi_v<float> * 0.5f)};
        const float right{std::sin(pan * std::numbers::pi_v<float> * 0.5f)};
        const float attenuation{1.0f / (1.0f + (distance / HALF_LOUDNESS_METRES))};

        const float toward{-sound.velocity.dot(direction)}; // positive when approaching the ear
        const float doppler{SPEED_OF_SOUND / std::max(SPEED_OF_SOUND - toward, SPEED_OF_SOUND * 0.25f)};

        Voice voice{};
        voice.kind = sound.kind;
        const float rate{static_cast<float>(m_sample_rate)};
        switch (sound.kind) {
        case SoundKind::Ping: {
            voice.frequency = pitchOf(sound.creature_id) * doppler;
            const float gain{std::clamp(sound.strength, 0.0f, 1.0f) * PING_GAIN * attenuation};
            voice.gain_left = gain * left;
            voice.gain_right = gain * right;
            voice.decay = std::exp(-1.0f / (PING_DECAY_SECONDS * rate));
            voice.remaining = static_cast<std::uint32_t>(PING_SECONDS * rate);
            break;
        }
        case SoundKind::Scratch: {
            voice.frequency = SCRATCH_CUTOFF_HZ * doppler;
            const float gain{std::clamp(sound.strength, 0.0f, 1.0f) * SCRATCH_GAIN * attenuation};
            voice.gain_left = gain * left;
            voice.gain_right = gain * right;
            voice.decay = std::exp(-1.0f / (SCRATCH_DECAY_SECONDS * rate));
            voice.remaining = static_cast<std::uint32_t>(SCRATCH_SECONDS * rate);
            break;
        }
        }
        return voice;
    }

    void Mixer::render(float* const out, const std::uint32_t frames)
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        std::fill(out, out + (static_cast<std::size_t>(frames) * 2u), 0.0f);

        const float rate{static_cast<float>(m_sample_rate)};
        for (Voice& voice : m_voices) {
            const std::uint32_t count{std::min(frames, voice.remaining)};
            switch (voice.kind) {
            case SoundKind::Ping: {
                const float step{2.0f * std::numbers::pi_v<float> * voice.frequency / rate};
                for (std::uint32_t frame{0u}; frame < count; ++frame) {
                    const float sample{std::sin(voice.phase) * voice.envelope};
                    voice.phase += step;
                    if (voice.phase > 2.0f * std::numbers::pi_v<float>) {
                        voice.phase -= 2.0f * std::numbers::pi_v<float>;
                    }
                    voice.envelope *= voice.decay;
                    out[(frame * 2u)] += sample * voice.gain_left;
                    out[(frame * 2u) + 1u] += sample * voice.gain_right;
                }
                break;
            }
            case SoundKind::Scratch: {
                // White noise through a one-pole low-pass at the (Doppler-shifted) cutoff.
                const float alpha{std::clamp(2.0f * std::numbers::pi_v<float> * voice.frequency / rate, 0.0f, 1.0f)};
                for (std::uint32_t frame{0u}; frame < count; ++frame) {
                    m_noise = (m_noise * 1664525u) + 1013904223u;
                    const float white{(static_cast<float>(m_noise >> 8u) / 8388607.5f) - 1.0f};
                    voice.lowpass += alpha * (white - voice.lowpass);
                    const float sample{voice.lowpass * voice.envelope};
                    voice.envelope *= voice.decay;
                    out[(frame * 2u)] += sample * voice.gain_left;
                    out[(frame * 2u) + 1u] += sample * voice.gain_right;
                }
                break;
            }
            }
            voice.remaining -= count;
        }
        std::erase_if(m_voices, [](const Voice& voice) {
            return voice.remaining == 0u;
        });

        // A soft ceiling: many voices at once must never clip into the speakers.
        for (std::uint32_t sample{0u}; sample < frames * 2u; ++sample) {
            out[sample] = std::tanh(out[sample]);
        }
    }

    std::size_t Mixer::liveVoices()
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        return m_voices.size();
    }

} // namespace AudioLib
