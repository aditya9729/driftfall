// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "platform/input.hpp"

#include <algorithm>
#include <cmath>

namespace df {

void Input::set_viewport(i32 width, i32 height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
}

void Input::begin_frame(f32 dt) {
    // Look is a per-frame delta and the button presses are edges: all three
    // must be cleared or a single tap fires every frame until the next touch.
    state_.look = vec2{0.0f};
    state_.reload_pressed = false;
    state_.build_pressed = false;
    state_.hud_toggle_pressed = false;

    if (move_finger_.active) move_finger_.held_seconds += dt;
    if (look_finger_.active) look_finger_.held_seconds += dt;
    if (aux_finger_.active) aux_finger_.held_seconds += dt;
}

Input::Finger* Input::find(SDL_FingerID id) {
    if (move_finger_.active && move_finger_.id == id) return &move_finger_;
    if (look_finger_.active && look_finger_.id == id) return &look_finger_;
    if (aux_finger_.active && aux_finger_.id == id) return &aux_finger_;
    return nullptr;
}

bool Input::handle_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_QUIT:
            state_.quit = true;
            return true;

        // --- touch ---------------------------------------------------------
        case SDL_EVENT_FINGER_DOWN: {
            // SDL reports normalised touch coordinates; convert once, here.
            const vec2 point{event.tfinger.x * static_cast<f32>(width_),
                             event.tfinger.y * static_cast<f32>(height_)};
            const bool left_half = point.x < static_cast<f32>(width_) * 0.5f;

            // One stick finger per half; a second finger on the move half
            // becomes the aux slot rather than being dropped on the floor.
            Finger* slot = nullptr;
            FingerRole role = FingerRole::Look;
            if (left_half) {
                if (!move_finger_.active) {
                    slot = &move_finger_;
                    role = FingerRole::Move;
                } else if (!aux_finger_.active) {
                    slot = &aux_finger_;
                    role = FingerRole::Aux;
                }
            } else if (!look_finger_.active) {
                slot = &look_finger_;
            }

            if (slot == nullptr) return true;

            *slot = Finger{event.tfinger.fingerID, point, point, 0.0f, true, role};
            if (role == FingerRole::Look) state_.firing = false;
            return true;
        }

        case SDL_EVENT_FINGER_MOTION: {
            Finger* finger = find(event.tfinger.fingerID);
            if (finger == nullptr) return false;

            const vec2 point{event.tfinger.x * static_cast<f32>(width_),
                             event.tfinger.y * static_cast<f32>(height_)};

            if (finger->role == FingerRole::Move) {
                const vec2 offset = point - finger->origin;
                const f32 length = std::sqrt(offset.x * offset.x + offset.y * offset.y);
                if (length > 0.0001f) {
                    const f32 magnitude = std::min(length, kStickRadiusPixels) / kStickRadiusPixels;
                    // Screen y grows downward; forward is up the screen.
                    state_.move = vec2{offset.x / length, -offset.y / length} * magnitude;
                }
            } else if (finger->role == FingerRole::Look) {
                const vec2 delta = point - finger->current;
                state_.look += vec2{delta.x, -delta.y} * kLookSensitivity;
                // Once you drag, it is a look, not a tap.
                state_.firing = true;
            }
            // Aux drives nothing while it is down; only the tap on release
            // means anything, and that needs the travel distance below.

            finger->current = point;
            return true;
        }

        case SDL_EVENT_FINGER_UP: {
            Finger* finger = find(event.tfinger.fingerID);
            if (finger == nullptr) return false;

            const vec2 travel = finger->current - finger->origin;
            const f32 distance = std::sqrt(travel.x * travel.x + travel.y * travel.y);
            const bool tapped = distance < kTapMaxPixels && finger->held_seconds < kTapMaxSeconds;

            switch (finger->role) {
                case FingerRole::Move:
                    state_.move = vec2{0.0f};
                    break;
                case FingerRole::Look:
                    // A quick tap on the look half is the reload gesture — the
                    // active-reload input has to be reachable without moving
                    // your aiming thumb anywhere.
                    if (tapped) state_.reload_pressed = true;
                    state_.firing = false;
                    break;
                case FingerRole::Aux:
                    // Second finger on the move half: the HUD toggle. Nothing
                    // in the game competes for this gesture, and a stray one
                    // costs you an overlay rather than a reload.
                    if (tapped) state_.hud_toggle_pressed = true;
                    break;
            }

            *finger = Finger{};
            return true;
        }

        // --- keyboard and mouse (desktop web + development) ----------------
        case SDL_EVENT_KEY_DOWN:
            if (event.key.repeat) return true;
            switch (event.key.key) {
                case SDLK_R:
                    state_.reload_pressed = true;
                    return true;
                case SDLK_B:
                    state_.build_pressed = true;
                    return true;
                case SDLK_F3:
                    state_.hud_toggle_pressed = true;
                    return true;
                case SDLK_ESCAPE:
                    state_.quit = true;
                    return true;
                default:
                    return false;
            }

        case SDL_EVENT_MOUSE_MOTION:
            if (!mouse_captured_) return false;
            state_.look += vec2{event.motion.xrel, -event.motion.yrel} * kLookSensitivity;
            return true;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                state_.firing = true;
                mouse_captured_ = true;
                return true;
            }
            return false;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                state_.firing = false;
                return true;
            }
            return false;

        default:
            return false;
    }
}

void Input::poll_keyboard() {
    // Touch drives movement directly through the stick; only fall back to keys
    // when no finger is on the move half.
    if (move_finger_.active) return;

    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys == nullptr) return;

    vec2 move{0.0f};
    if (keys[SDL_SCANCODE_W]) move.y += 1.0f;
    if (keys[SDL_SCANCODE_S]) move.y -= 1.0f;
    if (keys[SDL_SCANCODE_D]) move.x += 1.0f;
    if (keys[SDL_SCANCODE_A]) move.x -= 1.0f;

    const f32 length = std::sqrt(move.x * move.x + move.y * move.y);
    state_.move = length > 1.0f ? move / length : move;
}

}  // namespace df
