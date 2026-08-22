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

#include <math/vector.hpp>

#include <cstdint>
#include <mutex>
#include <vector>

/*!
    The spectator's ears: parametric sounds synthesised from the world's events, spatialised
    against the camera and pitch-shifted by Doppler from the replicated velocities.

    Presentation, not simulation - the ABI carries no auralisation and the creatures' own ears
    are the acoustics' business (TOPOLOGY.md § The spectator). Each EVENT becomes one short
    sound: a vocalisation is a ping, a pure tone whose pitch is the creature's own, decaying;
    a scratch is a burst of filtered noise as loud as the slide was. Both are placed by where
    they happened relative to the listener (equal-power pan, distance attenuation) and shifted
    by the source's velocity along the line to the listener - the owner's ruling that Doppler is
    realism owed now. No HRTF, no spectator-side occlusion: those keep their trigger.

    The mixer is host arithmetic with a seeded noise source, so a test can render it and count
    zero crossings; the device half is the platform's, behind `Output`.
*/
namespace AudioLib
{

    constexpr float SPEED_OF_SOUND{343.0f};

    //! Where the spectator listens from, and which way: the camera, published per frame.
    struct Listener {
        MathLib::Vec3 position{};
        MathLib::Vec3 forward{0.0f, 0.0f, -1.0f};
        MathLib::Vec3 right{1.0f, 0.0f, 0.0f};
    };

    enum class SoundKind : std::uint8_t {
        Ping, //!< A vocalisation: a decaying tone at the creature's own pitch.
        Scratch, //!< A slide along a face: a short burst of filtered noise.
    };

    //! One event as the mixer hears it: what, where, how fast the source moves, how loud.
    struct Sound {
        SoundKind kind{SoundKind::Ping};
        std::uint32_t creature_id{0u};
        MathLib::Vec3 position{};
        MathLib::Vec3 velocity{};
        float strength{0.0f};
    };

    //! The creature's own pitch: a semitone lattice from A4 by identity, so the same creature
    //! always sounds the same and two creatures are told apart by ear.
    [[nodiscard]] float pitchOf(std::uint32_t creature_id) noexcept;

    class Mixer {
    public:
        explicit Mixer(std::uint32_t sample_rate);

        //! The camera's pose this frame. Any thread; taken under the lock.
        void setListener(const Listener& listener);

        //! One event to sound. Any thread; the voice starts on the next render.
        void sound(const Sound& sound);

        /*!
            Mix `frames` stereo frames into `out` (interleaved L, R, in [-1, 1]), advancing every
            live voice and retiring the finished. The device thread's call; deterministic for a
            given sequence of sounds and listeners, which is what the tests render.
        */
        void render(float* out, std::uint32_t frames);

        [[nodiscard]] std::uint32_t sampleRate() const noexcept
        {
            return m_sample_rate;
        }

        //! Voices still sounding, for the log and the tests.
        [[nodiscard]] std::size_t liveVoices();

    private:
        struct Voice {
            SoundKind kind{SoundKind::Ping};
            float frequency{440.0f}; //!< Hz, Doppler applied at the start.
            float gain_left{0.0f};
            float gain_right{0.0f};
            float phase{0.0f}; //!< Radians, the ping's oscillator.
            float envelope{1.0f};
            float decay{0.0f}; //!< Per-sample multiplier on the envelope.
            float lowpass{0.0f}; //!< The scratch's one-pole state.
            std::uint32_t remaining{0u}; //!< Samples left before the voice retires.
        };

        [[nodiscard]] Voice voiceFor(const Sound& sound, const Listener& listener) const;

        std::uint32_t m_sample_rate;
        std::mutex m_mutex;
        Listener m_listener{};
        std::vector<Voice> m_voices;
        std::uint32_t m_noise{0x9E37'79B9u}; //!< The noise source's state: an LCG, seeded once.
    };

    //! The platform's output: a thread that pulls from the mixer and feeds the default render
    //! endpoint. Windows (WASAPI shared mode) today; elsewhere a silent stand-in that still
    //! drains the mixer, so the picture and the log behave alike everywhere.
    class Output {
    public:
        //! Opens the endpoint and starts the thread. Throws std::runtime_error when the platform
        //! has no endpoint to open - a headless machine, a missing device - in words.
        explicit Output(Mixer& mixer);
        ~Output();

        Output(const Output&) = delete;
        Output& operator=(const Output&) = delete;
        Output(Output&&) = delete;
        Output& operator=(Output&&) = delete;

        //! The endpoint's own rate, for the log; the mixer was built at it.
        [[nodiscard]] static std::uint32_t preferredSampleRate();

    private:
        struct Impl;
        Impl* m_impl{nullptr};
    };

} // namespace AudioLib
