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

#include "camera.hpp"
#include <math/quaternion.hpp>
#include <math/vector.hpp>
#include <cstdint>

/*!
    A scripted camera path for recording a clip of the world.

    Every term is periodic in the normalised time it takes, and every oscillation completes a whole
    number of cycles over that period, so the pose at time 1 is exactly the pose at time 0. A clip
    recorded from it loops without a seam, which is what an animation embedded in a README needs.

    This is a spectator-camera feature and nothing more. It records what a human sees; creatures
    have their own sensors and are never driven by it.
*/
class CinematicPath {
public:
    //! Where the camera is and how it is oriented at one instant.
    struct Pose {
        MathLib::Vec3 position{}; //!< World-space position, in metres.
        MathLib::Quat orientation{MathLib::Quat::identity()}; //!< Orientation, roll included.
    };

    /*!
        Returns the pose at a normalised time.

        \param time Position along the loop. Values outside [0, 1) wrap, so the caller need not
                    take a modulus of its own.
    */
    [[nodiscard]] Pose poseAt(float time) const;

    //! Applies the pose at a normalised time directly to a camera.
    void apply(Camera& camera, float time) const
    {
        const Pose pose{poseAt(time)};
        camera.setPose(pose.position, pose.orientation);
    }

private:
    /*
        The shape of the flight.

        A closed orbit around the geometry rather than a straight run, because a loop is the only
        path that can end where it began. The radius breathes and the height rises and falls so the
        orbit never reads as a turntable, and a slow bank keeps it from looking like something on
        rails — which it entirely is.

        The wobble is deliberately almost imperceptible. Enough of it reads as a hand on the
        controls; any more reads as a fault, and this footage exists to show the world rather than
        the camera work.
    */
    float m_orbit_radius{34.0f}; //!< Mean distance from the centre of the scene, in metres.
    float m_radius_breathe{5.0f}; //!< How far the radius swells and shrinks over the loop, in metres.
    /*
        Height is measured from the floor's base level, not from the ground beneath the camera,
        and the ground is no longer flat: the relief rises several metres. The mean sits high
        enough that the bottom of the swing still clears the highest terrace with room to spare,
        because a camera that clips through a hill mid-orbit ruins the one shot this path exists
        to take.
    */
    float m_height_mean{10.0f}; //!< Mean height above the floor's base level, in metres.
    float m_height_swing{2.6f}; //!< How far the height rises and falls, in metres.
    float m_look_ahead{0.14f}; //!< Fraction of the loop the camera aims ahead of itself.
    float m_inward_bias{0.55f}; //!< How much the aim is pulled towards the centre, from 0 to 1.
    float m_bank{0.15f}; //!< Roll amplitude through the turns, in radians.
    float m_wobble_yaw{0.010f}; //!< Yaw wobble amplitude, in radians.
    float m_wobble_pitch{0.008f}; //!< Pitch wobble amplitude, in radians.
    float m_wobble_roll{0.014f}; //!< Roll wobble amplitude, in radians.
};
