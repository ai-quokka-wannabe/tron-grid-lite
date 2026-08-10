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

#include "math/vector.hpp"
#include <cmath>

namespace MathLib
{

    //! Quaternion for 3D rotations — no gimbal lock.
    struct Quat {
        float w{1.0f}; //!< Scalar (real) part. Default is identity rotation.
        float x{0.0f}; //!< X component of the vector (imaginary) part.
        float y{0.0f}; //!< Y component of the vector (imaginary) part.
        float z{0.0f}; //!< Z component of the vector (imaginary) part.

        //! Returns the identity quaternion (no rotation).
        [[nodiscard]] static constexpr Quat identity()
        {
            return {1.0f, 0.0f, 0.0f, 0.0f};
        }

        //! Creates a quaternion from an axis and angle (radians).
        [[nodiscard]] static Quat fromAxisAngle(const Vec3& axis, float angle_radians)
        {
            Vec3 a = axis.normalised();
            float half_angle = angle_radians * 0.5f;
            float s = std::sin(half_angle);
            return {std::cos(half_angle), a.x * s, a.y * s, a.z * s};
        }

        //! Returns the dot product of two quaternions.
        [[nodiscard]] constexpr float dot(const Quat& other) const
        {
            return w * other.w + x * other.x + y * other.y + z * other.z;
        }

        //! Returns the squared length.
        [[nodiscard]] constexpr float lengthSquared() const
        {
            return dot(*this);
        }

        //! Returns the length.
        [[nodiscard]] float length() const
        {
            return std::sqrt(lengthSquared());
        }

        //! Returns a normalised copy. Returns identity if length is zero.
        [[nodiscard]] Quat normalised() const
        {
            float len = length();
            if (len == 0.0f) {
                return identity();
            }
            float inv = 1.0f / len;
            return {w * inv, x * inv, y * inv, z * inv};
        }

        //! Multiplies two quaternions (combines rotations).
        [[nodiscard]] constexpr Quat operator*(const Quat& other) const
        {
            return {
                w * other.w - x * other.x - y * other.y - z * other.z,
                w * other.x + x * other.w + y * other.z - z * other.y,
                w * other.y - x * other.z + y * other.w + z * other.x,
                w * other.z + x * other.y - y * other.x + z * other.w,
            };
        }

        //! Rotates a Vec3 by this quaternion.
        [[nodiscard]] constexpr Vec3 rotate(const Vec3& v) const
        {
            // q * v * q^-1, optimised (avoids full quaternion multiply)
            Vec3 qv{x, y, z};
            Vec3 t = qv.cross(v) * 2.0f;
            return v + t * w + qv.cross(t);
        }

        [[nodiscard]] constexpr bool operator==(const Quat& other) const = default;
    };

} // namespace MathLib
