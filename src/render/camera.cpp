// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "render/camera.hpp"

#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace df {

void Camera::set_yaw_pitch(f32 yaw, f32 pitch) {
    yaw_ = yaw;
    pitch_ = std::clamp(pitch, kMinPitch, kMaxPitch);
}

void Camera::add_yaw_pitch(f32 delta_yaw, f32 delta_pitch) {
    set_yaw_pitch(yaw_ + delta_yaw, pitch_ + delta_pitch);
}

void Camera::set_viewport(i32 width, i32 height) {
    if (height <= 0) return;
    aspect_ = static_cast<f32>(width) / static_cast<f32>(height);
}

vec3 Camera::forward() const {
    const f32 cos_pitch = std::cos(pitch_);
    return {cos_pitch * std::sin(yaw_), std::sin(pitch_), cos_pitch * std::cos(yaw_)};
}

vec3 Camera::flat_forward() const {
    return {std::sin(yaw_), 0.0f, std::cos(yaw_)};
}

vec3 Camera::flat_right() const {
    return {std::cos(yaw_), 0.0f, -std::sin(yaw_)};
}

void Camera::update(f32 dt) {
    const vec3 right = flat_right();
    const vec3 anchor = target_ + right * shoulder_offset_.x + vec3{0.0f, shoulder_offset_.y, 0.0f};
    const vec3 goal = anchor - forward() * distance_;

    // Exponential smoothing expressed against dt so a 30 Hz frame and a 120 Hz
    // frame settle at the same rate. The naive lerp(a, b, 0.1) does not.
    constexpr f32 kHalfLife = 0.06f;
    const f32 t = 1.0f - std::exp2(-dt / kHalfLife);
    eye_ += (goal - eye_) * t;
}

glm::mat4 Camera::view() const {
    const vec3 look_at = eye_ + forward();
    return glm::lookAtLH(eye_, look_at, vec3{0.0f, 1.0f, 0.0f});
}

glm::mat4 Camera::projection() const {
    // Clip-space depth is *not* the same on every backend bgfx targets:
    // OpenGL and WebGL2 use [-1,1] ("homogeneous depth"), while Metal, Vulkan
    // and Direct3D use [0,1]. Hardcoding the ZO variant, as this used to,
    // pushes the entire scene outside the depth range on the GL backends —
    // and the failure mode is silent, because the geometry is still submitted
    // and the draw-call count still looks healthy. It just never survives
    // clipping. Ask bgfx which convention the live backend wants.
    const bool homogeneous_depth = bgfx::getCaps()->homogeneousDepth;
    return homogeneous_depth ? glm::perspectiveLH_NO(fov_y_, aspect_, near_, far_)
                             : glm::perspectiveLH_ZO(fov_y_, aspect_, near_, far_);
}

}  // namespace df
