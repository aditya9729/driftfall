// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

#include <SDL3/SDL.h>

namespace df {

/// What the player is asking for this frame, in device-independent terms.
/// The rest of the game never sees a touch, a key, or a gamepad axis.
struct InputState {
    vec2 move{0.0f};  ///< normalised, -1..1 per axis
    vec2 look{0.0f};  ///< delta this frame, radians
    bool firing = false;
    bool reload_pressed = false;  ///< edge, not held
    bool build_pressed = false;   ///< edge
    bool quit = false;
};

/// Touch-first input.
///
/// Two floating sticks rather than fixed ones: the movement stick spawns
/// wherever your left thumb lands. Fixed sticks demand the player look at
/// their thumbs, and in a horde shooter looking away from the screen is death.
/// Keyboard and mouse are supported too, because that is how the web build
/// gets played on a desktop and how development happens at all.
class Input {
public:
    /// Clears per-frame edges and ages any fingers still down.
    void begin_frame(f32 dt);

    /// Feed every SDL event. Returns true if the event was consumed.
    bool handle_event(const SDL_Event& event);

    void set_viewport(i32 width, i32 height);

    /// Applies held-key movement. Call once per frame after event pumping.
    void poll_keyboard();

    [[nodiscard]] const InputState& state() const { return state_; }

    /// A drag shorter than this in both time and distance counts as a tap,
    /// which is the fire gesture on the look half of the screen.
    static constexpr f32 kTapMaxSeconds = 0.22f;
    static constexpr f32 kTapMaxPixels = 18.0f;

    static constexpr f32 kLookSensitivity = 0.0042f;
    static constexpr f32 kStickRadiusPixels = 90.0f;

private:
    struct Finger {
        SDL_FingerID id = 0;
        vec2 origin{0.0f};
        vec2 current{0.0f};
        f32 held_seconds = 0.0f;
        bool active = false;
        bool is_move_stick = false;
    };

    [[nodiscard]] Finger* find(SDL_FingerID id);

    InputState state_;
    Finger move_finger_;
    Finger look_finger_;
    i32 width_ = 1280;
    i32 height_ = 720;
    bool mouse_captured_ = false;
};

}  // namespace df
