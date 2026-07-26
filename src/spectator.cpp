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

#include "spectator.hpp"

void SpectatorController::processEvent(const WindowLib::WindowEvent& event)
{
    switch (event.type) {
    case WindowLib::WindowEvent::Type::KeyDown:
        // Key repeats carry no new information: the key is already in the held set.
        if (!event.key.repeat) {
            if (event.key.keycode == KEY_TAB) {
                // Route through the setter so that releasing the cursor always discards the look
                // accumulated beforehand, whichever path released it.
                setCursorCaptured(!m_cursor_captured);
            } else {
                m_keys_held.insert(event.key.keycode);
            }
        }
        break;

    case WindowLib::WindowEvent::Type::KeyUp:
        m_keys_held.erase(event.key.keycode);
        break;

    case WindowLib::WindowEvent::Type::MouseMove:
        // Mouse deltas are displacements that have already happened, so they are scaled by
        // sensitivity alone and never by the frame's delta time.
        if (m_cursor_captured) {
            m_pending_yaw -= (static_cast<float>(event.mouse_move.dx) * m_mouse_sensitivity);
            m_pending_pitch += (static_cast<float>(event.mouse_move.dy) * m_mouse_sensitivity);
        }
        break;

    case WindowLib::WindowEvent::Type::MouseButtonDown:
        if (event.mouse_button.button == MOUSE_BUTTON_RIGHT) {
            setCursorCaptured(!m_cursor_captured);
        }
        break;

    case WindowLib::WindowEvent::Type::Blur:
        // The observer has switched away from the window; drop everything so the camera does not
        // keep flying while nobody is watching.
        reset();
        setCursorCaptured(false);
        break;

    default:
        // Close, Resize, Expose, Focus, MouseButtonUp and None are of no interest to the spectator.
        break;
    }
}

void SpectatorController::update(Camera& camera, float delta_seconds)
{
    // Apply the accumulated mouse look first, so this frame's movement follows the new heading.
    if ((m_pending_yaw != 0.0f) || (m_pending_pitch != 0.0f)) {
        camera.rotate(m_pending_yaw, m_pending_pitch);
        m_pending_yaw = 0.0f;
        m_pending_pitch = 0.0f;
    }

    // Clamp the delta time so a breakpoint, a resize stall or a suspended window cannot fling the
    // camera across the world in a single step.
    float delta{delta_seconds};

    if (delta < 0.0f) {
        delta = 0.0f;
    } else if (delta > MAX_DELTA_SECONDS) {
        delta = MAX_DELTA_SECONDS;
    }

    float speed{m_move_speed};

    if (isHeld(KEY_SHIFT)) {
        speed *= m_fast_multiplier;
    }

    const float step{speed * delta};

    if (step == 0.0f) {
        return;
    }

    // Planar movement, relative to where the camera is looking.
    if (isHeld(KEY_W) || isHeld(KEY_UP)) {
        camera.moveForward(step);
    }

    if (isHeld(KEY_S) || isHeld(KEY_DOWN)) {
        camera.moveForward(-step);
    }

    if (isHeld(KEY_A) || isHeld(KEY_LEFT)) {
        camera.moveRight(-step);
    }

    if (isHeld(KEY_D) || isHeld(KEY_RIGHT)) {
        camera.moveRight(step);
    }

    // Vertical movement, along the world up axis, so ascending stays intuitive whatever the pitch.
    if (isHeld(KEY_E) || isHeld(KEY_SPACE)) {
        camera.moveUp(step);
    }

    if (isHeld(KEY_Q) || isHeld(KEY_CTRL)) {
        camera.moveUp(-step);
    }
}

void SpectatorController::setCursorCaptured(bool captured)
{
    m_cursor_captured = captured;

    if (!captured) {
        // Discard look accumulated up to the moment the cursor was released.
        m_pending_yaw = 0.0f;
        m_pending_pitch = 0.0f;
    }
}

bool SpectatorController::isMoving() const
{
    return (isHeld(KEY_W) || isHeld(KEY_A) || isHeld(KEY_S) || isHeld(KEY_D) || isHeld(KEY_UP) || isHeld(KEY_LEFT) || isHeld(KEY_DOWN) || isHeld(KEY_RIGHT)
        || isHeld(KEY_E) || isHeld(KEY_Q) || isHeld(KEY_SPACE) || isHeld(KEY_CTRL));
}

void SpectatorController::reset()
{
    m_keys_held.clear();
    m_pending_yaw = 0.0f;
    m_pending_pitch = 0.0f;
}

bool SpectatorController::isHeld(uint32_t keycode) const
{
    return m_keys_held.contains(keycode);
}
