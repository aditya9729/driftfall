// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

#include <glm/mat4x4.hpp>

namespace df {

/// Third-person over-the-shoulder camera.
///
/// Over-the-shoulder rather than first-person on purpose: on a phone you are
/// holding the device with the same thumbs you aim with, and seeing your own
/// silhouette is what makes cover legible. It is also the Gears framing, which
/// is the framing this game is quoting.
class Camera {
public:
    void set_target(vec3 target) { target_ = target; }

    void set_yaw_pitch(f32 yaw, f32 pitch);

    void add_yaw_pitch(f32 delta_yaw, f32 delta_pitch);

    /// Moves the camera toward its goal. Smoothing is frame-rate independent
    /// so the feel does not change when the device throttles.
    void update(f32 dt);

    void set_viewport(i32 width, i32 height);

    [[nodiscard]] glm::mat4 view() const;

    [[nodiscard]] glm::mat4 projection() const;

    [[nodiscard]] vec3 eye() const { return eye_; }

    [[nodiscard]] vec3 forward() const;

    /// Horizontal forward/right, for translating stick input into movement
    /// that ignores pitch.
    [[nodiscard]] vec3 flat_forward() const;

    [[nodiscard]] vec3 flat_right() const;

    /// The full camera basis, matching view(). The sky pass builds its view
    /// rays out of these rather than inverting the view-projection, which
    /// would have to be got right separately per clip-space convention.
    [[nodiscard]] vec3 right() const;

    [[nodiscard]] vec3 up() const;

    [[nodiscard]] f32 fov_y() const { return fov_y_; }

    [[nodiscard]] f32 aspect() const { return aspect_; }

    static constexpr f32 kMinPitch = -1.35f;
    static constexpr f32 kMaxPitch = 1.25f;

private:
    vec3 target_{0.0f};
    vec3 eye_{0.0f, 4.0f, 8.0f};
    f32 yaw_ = 0.0f;
    f32 pitch_ = -0.25f;
    f32 distance_ = 5.5f;
    /// Over the right shoulder, in camera space.
    vec3 shoulder_offset_{0.9f, 1.7f, 0.0f};
    f32 aspect_ = 16.0f / 9.0f;
    f32 fov_y_ = 1.13f;  // ~65 degrees
    f32 near_ = 0.1f;
    f32 far_ = 320.0f;
};

}  // namespace df
