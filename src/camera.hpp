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

#pragma once

#include <math/quaternion.hpp>
#include <math/vector.hpp>

/*!
    Free-flight debug camera with quaternion orientation.

    This is the camera the User looks through in the debug window, and during development to
    inspect the Grid. It is not a player camera: the Grid is inhabited exclusively by creatures
    driven by Programs, and the User never controls an inhabitant. The camera exists purely so
    that a developer can fly around, watch what the creatures do, and verify that the renderer
    behaves.

    Supports WASD + mouse look. Movement is in local camera space (forward/right relative to where
    the camera is looking), with vertical movement along the world up axis. Rotation is
    quaternion-based — no gimbal lock.

    Note that creature sensors do not use this class. Each creature's eye derives its own view and
    projection matrices directly from that creature's own state (position, head orientation, field
    of view), and renders at its own tiny sensor resolution. Changing the User's camera therefore
    has no effect whatsoever on what any creature perceives.
*/
class Camera {
public:
    //! Constructs a debug camera at the given position looking along -Z.
    explicit Camera(const MathLib::Vec3& position = {0.0f, 0.0f, 0.0f}, float fov_y = MathLib::PI / 4.0f) :
        m_position(position),
        m_fov_y(fov_y)
    {
    }

    //! Moves the camera forward (along local -Z axis).
    void moveForward(float delta)
    {
        MathLib::Vec3 forward = m_orientation.rotate({0.0f, 0.0f, -1.0f});
        m_position += forward * delta;
    }

    //! Moves the camera right (along local +X axis).
    void moveRight(float delta)
    {
        MathLib::Vec3 right = m_orientation.rotate({1.0f, 0.0f, 0.0f});
        m_position += right * delta;
    }

    //! Moves the camera up (along world +Y axis).
    void moveUp(float delta)
    {
        m_position.y += delta;
    }

    //! Rotates the camera by yaw (horizontal) and pitch (vertical) in radians.
    void rotate(float yaw, float pitch)
    {
        // Yaw around world Y axis (so horizontal look is always level).
        MathLib::Quat yaw_quat = MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);

        // Pitch around local X axis (camera's right vector).
        MathLib::Quat pitch_quat = MathLib::Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitch);

        // Apply yaw first (world space), then pitch (local space).
        m_orientation = (yaw_quat * m_orientation * pitch_quat).normalised();
    }

    /*!
        Places the camera at an absolute pose.

        The interactive controls above are all incremental, which is what the User flying a camera
        wants. A scripted path is the opposite: it knows exactly where the camera belongs at a given
        moment, including a roll that no keyboard control produces.
    */
    void setPose(const MathLib::Vec3& position, const MathLib::Quat& orientation)
    {
        m_position = position;
        m_orientation = orientation.normalised();
    }

    //! Camera position in world space, in metres.
    [[nodiscard]] const MathLib::Vec3& position() const
    {
        return m_position;
    }

    //! Camera orientation quaternion.
    [[nodiscard]] const MathLib::Quat& orientation() const
    {
        return m_orientation;
    }

    //! Vertical field of view in radians.
    [[nodiscard]] float fovY() const
    {
        return m_fov_y;
    }

private:
    MathLib::Vec3 m_position{0.0f, 0.0f, 0.0f}; //!< World-space position, in metres.
    MathLib::Quat m_orientation{MathLib::Quat::identity()}; //!< Orientation (default: looking along -Z).
    float m_fov_y{MathLib::PI / 4.0f}; //!< Vertical field of view in radians.
};
