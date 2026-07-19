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
#include <cstdint>
#include <unordered_set>
#include <window/window_event.hpp>

/*!
    Input controller for the human observer's free-flight spectator camera.

    This is the only interactive element in the entire project, and it exists purely for
    development and observation. A human being sitting at the keyboard flies this camera around
    the world to watch what happens, to inspect geometry, and to verify that the renderer behaves
    correctly.

    It is emphatically not a player character. The world is inhabited exclusively by creatures
    driven by their own agent plugins; those creatures receive their motor intent across the plugin
    interface and never from this class. Nothing typed at the keyboard can influence any creature,
    and moving the spectator camera has no effect whatsoever on what any creature perceives —
    creature sensors derive their own view and projection matrices from their own state (see
    docs/PERCEPTION.md). The spectator is a window onto the world, never a hand inside it.

    The controller owns only input state: a set of currently-held keys, a desired cursor-capture
    flag, and the accumulated mouse-look deltas. It holds no window, no camera and no renderer
    resources. Feed it events with processEvent(), then call update() once per frame with the
    frame's delta time and the Camera to drive.

    Movement is frame-rate independent: every translation is scaled by the delta time in seconds.
    Mouse look deliberately is not scaled by delta time, because a mouse delta is already a
    displacement that has happened rather than a rate.
*/
class SpectatorController {
public:
    /*!
        Key codes follow the window library's keycode convention: the raw platform key code that
        WindowLib::WindowEvent::KeyData carries (Win32 virtual-key codes on Windows, X11 hardware
        key codes on Linux). This is the one and only mapping block — change a binding here and it
        changes everywhere.
    */
#ifdef _WIN32
    static constexpr uint32_t KEY_W{0x57}; //!< Move forward.
    static constexpr uint32_t KEY_A{0x41}; //!< Strafe left.
    static constexpr uint32_t KEY_S{0x53}; //!< Move backward.
    static constexpr uint32_t KEY_D{0x44}; //!< Strafe right.
    static constexpr uint32_t KEY_UP{0x26}; //!< Move forward (alternative).
    static constexpr uint32_t KEY_LEFT{0x25}; //!< Strafe left (alternative).
    static constexpr uint32_t KEY_DOWN{0x28}; //!< Move backward (alternative).
    static constexpr uint32_t KEY_RIGHT{0x27}; //!< Strafe right (alternative).
    static constexpr uint32_t KEY_E{0x45}; //!< Ascend.
    static constexpr uint32_t KEY_Q{0x51}; //!< Descend.
    static constexpr uint32_t KEY_SPACE{0x20}; //!< Ascend (alternative).
    static constexpr uint32_t KEY_CTRL{0x11}; //!< Descend (alternative).
    static constexpr uint32_t KEY_SHIFT{0x10}; //!< Fast-movement modifier.
    static constexpr uint32_t KEY_TAB{0x09}; //!< Toggle cursor capture.
#else
    static constexpr uint32_t KEY_W{25}; //!< Move forward.
    static constexpr uint32_t KEY_A{38}; //!< Strafe left.
    static constexpr uint32_t KEY_S{39}; //!< Move backward.
    static constexpr uint32_t KEY_D{40}; //!< Strafe right.
    static constexpr uint32_t KEY_UP{111}; //!< Move forward (alternative).
    static constexpr uint32_t KEY_LEFT{113}; //!< Strafe left (alternative).
    static constexpr uint32_t KEY_DOWN{116}; //!< Move backward (alternative).
    static constexpr uint32_t KEY_RIGHT{114}; //!< Strafe right (alternative).
    static constexpr uint32_t KEY_E{26}; //!< Ascend.
    static constexpr uint32_t KEY_Q{24}; //!< Descend.
    static constexpr uint32_t KEY_SPACE{65}; //!< Ascend (alternative).
    static constexpr uint32_t KEY_CTRL{37}; //!< Descend (alternative).
    static constexpr uint32_t KEY_SHIFT{50}; //!< Fast-movement modifier.
    static constexpr uint32_t KEY_TAB{23}; //!< Toggle cursor capture.
#endif

    static constexpr uint8_t MOUSE_BUTTON_RIGHT{1}; //!< Right mouse button also toggles cursor capture.

    static constexpr float DEFAULT_MOVE_SPEED{10.0f}; //!< Base movement speed, in metres per second.
    static constexpr float DEFAULT_FAST_MULTIPLIER{5.0f}; //!< Speed multiplier while the fast modifier is held.
    static constexpr float DEFAULT_MOUSE_SENSITIVITY{0.002f}; //!< Mouse look scale, in radians per pixel of cursor displacement.
    static constexpr float MAX_DELTA_SECONDS{0.1f}; //!< Delta-time clamp, in seconds, so a stall cannot teleport the camera.

    //! Constructs a controller with the default speeds and sensitivity.
    SpectatorController() = default;

    /*!
        Consumes one window event and folds it into the input state.

        Key presses and releases maintain the held-key set, mouse movement accumulates look deltas
        while the cursor is captured, and the capture toggle flips the desired capture state. Focus
        loss releases every held key so the camera does not drift while the window is in the
        background. Events the spectator does not care about are ignored.

        Note that key repeats are only flagged on Win32; the X11 backend reports every auto-repeat
        as a fresh press (with an intervening release), so holding the capture-toggle key down on
        Linux repeatedly flips the capture state. Tap the key rather than holding it. Suppressing
        that properly means detecting auto-repeat in the window library, not here.
    */
    void processEvent(const WindowLib::WindowEvent& event);

    /*!
        Applies the accumulated input to the given camera and clears the pending mouse look.

        Translation is scaled by delta_seconds (clamped to MAX_DELTA_SECONDS) so motion is
        frame-rate independent; rotation is not, because mouse deltas are already displacements.

        Pitch is deliberately left unclamped: Camera owns the orientation as a quaternion and exposes
        no pitch angle to clamp against, so looking far enough up or down rolls the view past
        vertical. That is harmless for an observer tool, and a pitch limit belongs in Camera if it is
        ever wanted.

        Must be called once per frame even when no key is held, because it is what drains the pending
        mouse look.
    */
    void update(Camera& camera, float delta_seconds);

    //! Returns true if the controller currently wants the cursor captured for mouse look.
    [[nodiscard]] bool cursorCaptured() const
    {
        return m_cursor_captured;
    }

    /*!
        Forces the desired cursor-capture state, for example to synchronise with the window after a
        focus change.

        Releasing the cursor discards any look accumulated beforehand, so the camera cannot lurch on
        the frame the cursor is released. Every capture change inside this class routes through here,
        so that guarantee holds for the Tab and right-button toggles too.
    */
    void setCursorCaptured(bool captured);

    //! Returns true if any bound movement key is currently held.
    [[nodiscard]] bool isMoving() const;

    //! Releases every held key and discards pending mouse look, leaving the camera stationary.
    void reset();

    //! Base movement speed, in metres per second.
    [[nodiscard]] float moveSpeed() const
    {
        return m_move_speed;
    }

    //! Sets the base movement speed, in metres per second.
    void setMoveSpeed(float speed)
    {
        m_move_speed = speed;
    }

    //! Multiplier applied to the movement speed while the fast modifier is held.
    [[nodiscard]] float fastMultiplier() const
    {
        return m_fast_multiplier;
    }

    //! Sets the fast-movement multiplier.
    void setFastMultiplier(float multiplier)
    {
        m_fast_multiplier = multiplier;
    }

    //! Mouse look sensitivity, in radians per pixel.
    [[nodiscard]] float mouseSensitivity() const
    {
        return m_mouse_sensitivity;
    }

    //! Sets the mouse look sensitivity, in radians per pixel.
    void setMouseSensitivity(float sensitivity)
    {
        m_mouse_sensitivity = sensitivity;
    }

private:
    //! Returns true if the given key code is currently held down.
    [[nodiscard]] bool isHeld(uint32_t keycode) const;

    std::unordered_set<uint32_t> m_keys_held{}; //!< Key codes currently held down.
    bool m_cursor_captured{false}; //!< True while the cursor is grabbed for mouse look.
    float m_pending_yaw{0.0f}; //!< Accumulated yaw since the last update, in radians.
    float m_pending_pitch{0.0f}; //!< Accumulated pitch since the last update, in radians.
    float m_move_speed{DEFAULT_MOVE_SPEED}; //!< Base movement speed, in metres per second.
    float m_fast_multiplier{DEFAULT_FAST_MULTIPLIER}; //!< Fast modifier multiplier.
    float m_mouse_sensitivity{DEFAULT_MOUSE_SENSITIVITY}; //!< Mouse look scale, in radians per pixel.
};
