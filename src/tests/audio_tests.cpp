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
    The spectator's ears without speakers: the mixer rendered into a buffer and measured. A
    sound to the right is louder on the right; a far one is quieter; an approaching one is
    higher - counted in zero crossings, not trusted in a formula; silence is zeros; a voice
    retires when it is done.
*/

#include "../audio.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

#include <testing/testing.hpp>

namespace
{

    constexpr std::uint32_t RATE{48'000u};

    struct Channels {
        float left{0.0f};
        float right{0.0f};
    };

    //! Peak absolute sample per channel over a render.
    [[nodiscard]] Channels peaks(const std::vector<float>& stereo)
    {
        Channels result{};
        for (std::size_t frame{0u}; frame < stereo.size() / 2u; ++frame) {
            result.left = std::max(result.left, std::fabs(stereo[frame * 2u]));
            result.right = std::max(result.right, std::fabs(stereo[(frame * 2u) + 1u]));
        }
        return result;
    }

    //! Zero crossings of the left channel: twice the cycles, so a pitch to count.
    [[nodiscard]] std::uint32_t crossings(const std::vector<float>& stereo)
    {
        std::uint32_t count{0u};
        for (std::size_t frame{1u}; frame < stereo.size() / 2u; ++frame) {
            const float a{stereo[(frame - 1u) * 2u]};
            const float b{stereo[frame * 2u]};
            if ((a < 0.0f && b >= 0.0f) || (a >= 0.0f && b < 0.0f)) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::vector<float> renderOne(const AudioLib::Sound& sound, const AudioLib::Listener& listener, const std::uint32_t frames)
    {
        AudioLib::Mixer mixer{RATE};
        mixer.setListener(listener);
        mixer.sound(sound);
        std::vector<float> stereo(static_cast<std::size_t>(frames) * 2u, 0.0f);
        mixer.render(stereo.data(), frames);
        return stereo;
    }

}

TEST_CASE(silence_is_zeros_and_a_finished_voice_retires)
{
    AudioLib::Mixer mixer{RATE};
    std::vector<float> stereo(2u * 512u, 1.0f);
    mixer.render(stereo.data(), 512u);
    for (const float sample : stereo) {
        TEST_CHECK(sample == 0.0f);
    }
    mixer.sound(AudioLib::Sound{.kind = AudioLib::SoundKind::Ping, .creature_id = 1u, .position = {0.0f, 0.0f, -1.0f}, .velocity = {}, .strength = 1.0f});
    TEST_CHECK_EQUAL(mixer.liveVoices(), static_cast<std::size_t>(1u));
    std::vector<float> long_render(2u * RATE, 0.0f); // a whole second: longer than any voice
    mixer.render(long_render.data(), RATE);
    TEST_CHECK_EQUAL(mixer.liveVoices(), static_cast<std::size_t>(0u));
    TEST_CHECK(peaks(long_render).left > 0.0f);
}

TEST_CASE(a_sound_to_the_right_is_louder_on_the_right_and_a_far_one_is_quieter)
{
    const AudioLib::Listener listener{};
    const AudioLib::Sound right{.kind = AudioLib::SoundKind::Ping, .creature_id = 3u, .position = {2.0f, 0.0f, 0.0f}, .velocity = {}, .strength = 1.0f};
    const Channels right_peaks{peaks(renderOne(right, listener, 2048u))};
    TEST_CHECK(right_peaks.right > right_peaks.left * 4.0f);

    const AudioLib::Sound ahead{.kind = AudioLib::SoundKind::Ping, .creature_id = 3u, .position = {0.0f, 0.0f, -2.0f}, .velocity = {}, .strength = 1.0f};
    const Channels ahead_peaks{peaks(renderOne(ahead, listener, 2048u))};
    TEST_CHECK_CLOSE(ahead_peaks.left, ahead_peaks.right, 1e-4f);

    const AudioLib::Sound far{.kind = AudioLib::SoundKind::Ping, .creature_id = 3u, .position = {0.0f, 0.0f, -40.0f}, .velocity = {}, .strength = 1.0f};
    TEST_CHECK(peaks(renderOne(far, listener, 2048u)).left < ahead_peaks.left * 0.25f);

    // A scratch is a sound too, and a silent strength is silence.
    const AudioLib::Sound scratch{.kind = AudioLib::SoundKind::Scratch, .creature_id = 3u, .position = {0.0f, 0.0f, -2.0f}, .velocity = {}, .strength = 0.5f};
    TEST_CHECK(peaks(renderOne(scratch, listener, 2048u)).left > 0.0f);
    const AudioLib::Sound mute{.kind = AudioLib::SoundKind::Ping, .creature_id = 3u, .position = {0.0f, 0.0f, -2.0f}, .velocity = {}, .strength = 0.0f};
    TEST_CHECK(peaks(renderOne(mute, listener, 2048u)).left == 0.0f);
}

TEST_CASE(an_approaching_source_is_higher_and_a_receding_one_lower)
{
    const AudioLib::Listener listener{};
    // Ahead, at 20 m: approaching at 34.3 m/s (a tenth of sound) raises the pitch a tenth.
    const AudioLib::Sound still{.kind = AudioLib::SoundKind::Ping, .creature_id = 0u, .position = {0.0f, 0.0f, -20.0f}, .velocity = {}, .strength = 1.0f};
    const AudioLib::Sound approaching{
        .kind = AudioLib::SoundKind::Ping, .creature_id = 0u, .position = {0.0f, 0.0f, -20.0f}, .velocity = {0.0f, 0.0f, 34.3f}, .strength = 1.0f};
    const AudioLib::Sound receding{
        .kind = AudioLib::SoundKind::Ping, .creature_id = 0u, .position = {0.0f, 0.0f, -20.0f}, .velocity = {0.0f, 0.0f, -34.3f}, .strength = 1.0f};

    constexpr std::uint32_t FRAMES{RATE / 10u}; // a tenth of a second: 44 cycles of A4 = 88 crossings
    const std::uint32_t base{crossings(renderOne(still, listener, FRAMES))};
    const std::uint32_t up{crossings(renderOne(approaching, listener, FRAMES))};
    const std::uint32_t down{crossings(renderOne(receding, listener, FRAMES))};
    TEST_CHECK(base >= 86u && base <= 90u);
    TEST_CHECK(up > base + 6u); // 440 * 1/0.9 = 489 Hz: ~98 crossings
    TEST_CHECK(down < base - 6u); // 440 * 1/1.1 = 400 Hz: ~80 crossings
    // And a sideways source, not closing, keeps its pitch exactly.
    const AudioLib::Sound sideways{
        .kind = AudioLib::SoundKind::Ping, .creature_id = 0u, .position = {0.0f, 0.0f, -20.0f}, .velocity = {34.3f, 0.0f, 0.0f}, .strength = 1.0f};
    TEST_CHECK_EQUAL(crossings(renderOne(sideways, listener, FRAMES)), base);
}

TEST_CASE(every_creature_has_its_own_pitch_within_an_octave)
{
    TEST_CHECK_CLOSE(AudioLib::pitchOf(0u), 440.0f, 1e-3f);
    TEST_CHECK_CLOSE(AudioLib::pitchOf(12u), 440.0f, 1e-3f);
    TEST_CHECK(AudioLib::pitchOf(7u) > AudioLib::pitchOf(6u));
    TEST_CHECK(AudioLib::pitchOf(11u) < 880.0f);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
