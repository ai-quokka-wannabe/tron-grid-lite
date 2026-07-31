/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#include "cinematic.hpp"
#include <algorithm>
#include <cmath>

namespace
{

    //! One full turn, in radians.
    constexpr float TWO_PI{6.283185307179586f};

    //! Where the camera aims when it is pulled inwards: the height of the standing geometry.
    constexpr MathLib::Vec3 SCENE_HEART{0.0f, 4.0f, 0.0f};

    //! Wraps a normalised time into [0, 1).
    [[nodiscard]] float wrap(float time)
    {
        const float fractional{time - std::floor(time)};
        return std::clamp(fractional, 0.0f, 1.0f);
    }

} // namespace

CinematicPath::Pose CinematicPath::poseAt(float time) const
{
    const float t{wrap(time)};

    /*
        Where the camera sits.

        Both modulations complete a whole number of cycles, so the orbit closes on itself: the
        radius swells twice and the height rises and falls three times over one loop, which is
        enough to keep a circle from reading as a turntable shot.
    */
    const auto positionAt = [this](float at) {
        const float angle{TWO_PI * at};
        const float radius{m_orbit_radius + (m_radius_breathe * std::sin(2.0f * angle))};
        const float height{m_height_mean + (m_height_swing * std::sin(3.0f * angle))};
        return MathLib::Vec3{radius * std::cos(angle), height, radius * std::sin(angle)};
    };

    const MathLib::Vec3 position{positionAt(t)};

    /*
        Where it looks.

        Straight along the orbit would swing the geometry past the edge of frame; straight at the
        centre would be a turntable. Aiming at a point some way ahead on the path and then pulling
        that aim towards the scene keeps the pillars in shot while the camera still reads as
        travelling rather than orbiting.
    */
    const MathLib::Vec3 ahead{positionAt(t + m_look_ahead)};
    const MathLib::Vec3 target{ahead + ((SCENE_HEART - ahead) * m_inward_bias)};

    MathLib::Vec3 forward{target - position};
    const float distance{forward.length()};
    forward = (distance > 1e-4f) ? (forward * (1.0f / distance)) : MathLib::Vec3{0.0f, 0.0f, -1.0f};

    // An identity orientation looks along -Z, so the yaw and pitch that produce this forward vector
    // are recovered against that convention rather than against +Z.
    const float yaw{std::atan2(-forward.x, -forward.z) + (m_wobble_yaw * std::sin(5.0f * TWO_PI * t))};
    const float pitch{std::asin(std::clamp(forward.y, -1.0f, 1.0f)) + (m_wobble_pitch * std::sin(4.0f * TWO_PI * t))};

    // Banking. A circular orbit turns the same way throughout, so a constant roll would be correct
    // and lifeless; letting it swing twice per loop reads as a craft leaning through its turns.
    const float roll{(m_bank * std::sin(2.0f * TWO_PI * t)) + (m_wobble_roll * std::sin(7.0f * TWO_PI * t))};

    const MathLib::Quat yaw_quat{MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yaw)};
    const MathLib::Quat pitch_quat{MathLib::Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitch)};
    const MathLib::Quat roll_quat{MathLib::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, roll)};

    // Yaw in world space, then pitch and roll in the camera's own frame, matching the order the
    // interactive controls use so that a scripted pose and a flown one mean the same thing.
    Pose pose{};
    pose.position = position;
    pose.orientation = (yaw_quat * pitch_quat * roll_quat).normalised();
    return pose;
}
