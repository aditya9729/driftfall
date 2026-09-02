// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "render/camera.hpp"

#include "voxel/raycast.hpp"

#include <bgfx/bgfx.h>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace df {
namespace {

/// Below this the arm has no direction to speak of and normalising it would
/// produce garbage. Only reachable if the target teleports onto the eye.
constexpr f32 kDegenerateArm = 1.0e-5f;

/// 1 - 2^(-dt/half_life): the fraction of the remaining gap to close this
/// step. Expressed against dt so a 30 Hz frame and a 120 Hz frame settle at
/// the same rate; the naive lerp(a, b, 0.1) does not.
f32 smoothing_factor(f32 dt, f32 half_life) {
    if (!(dt > 0.0f)) return 0.0f;
    return 1.0f - std::exp2(-dt / half_life);
}

}  // namespace

glm::mat4 make_projection(f32 fov_y, f32 aspect, f32 near_z, f32 far_z, bool homogeneous_depth) {
    // Clip-space depth is *not* the same on every backend bgfx targets:
    // OpenGL and WebGL2 use [-1,1] ("homogeneous depth"), while Metal, Vulkan
    // and Direct3D use [0,1]. Hardcoding the ZO variant, as this used to,
    // pushes the entire scene outside the depth range on the GL backends —
    // and the failure mode is silent, because the geometry is still submitted
    // and the draw-call count still looks healthy. It just never survives
    // clipping.
    return homogeneous_depth ? glm::perspectiveLH_NO(fov_y, aspect, near_z, far_z)
                             : glm::perspectiveLH_ZO(fov_y, aspect, near_z, far_z);
}

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

vec3 Camera::right() const {
    // Matches glm::lookAtLH's basis: x = normalize(cross(up, forward)).
    return glm::normalize(glm::cross(vec3{0.0f, 1.0f, 0.0f}, forward()));
}

vec3 Camera::up() const {
    return glm::cross(forward(), right());
}

vec3 Camera::head() const {
    // The lateral offset uses flat_right(), not right(), so looking up does
    // not swing the shoulder through the player's body.
    return target_ + flat_right() * shoulder_offset_.x + vec3{0.0f, shoulder_offset_.y, 0.0f};
}

f32 Camera::near_plane_radius() const {
    const f32 half_h = near_ * std::tan(fov_y_ * 0.5f);
    const f32 half_w = half_h * aspect_;
    return std::sqrt(half_h * half_h + half_w * half_w);
}

f32 Camera::eye_skin() const {
    return near_plane_radius() + kSkinMargin;
}

f32 Camera::arm_length() const {
    return glm::length(eye_ - head());
}

void Camera::update(f32 dt, const VoxelWorld* world) {
    const vec3 anchor = head();
    const vec3 goal = anchor - forward() * distance_;

    // Stage one: the follow filter, unchanged from before the spring arm. It
    // runs on free_eye_ — where the eye would be in an empty world — so that
    // geometry never enters the filter's state. A wall that appears and
    // vanishes leaves no trace in the follow behaviour.
    free_eye_ += (goal - free_eye_) * smoothing_factor(dt, kFollowHalfLife);

    const vec3 arm = free_eye_ - anchor;
    const f32 free_length = glm::length(arm);
    if (!(free_length > kDegenerateArm)) {
        eye_ = anchor;
        return;
    }
    const vec3 dir = arm / free_length;

    // Stage two: how much of that arm the geometry leaves us, as a *fraction*
    // rather than a length.
    //
    // The fraction is what makes the arm inert when nothing is in the way. An
    // absolute length has to chase free_length, and free_length is not
    // constant — it grows whenever the player accelerates, because that is
    // exactly what the follow lag does. Chasing it would add a second, slower
    // lag to ordinary running and the camera would breathe in and out every
    // time you started moving. In fraction space the unconstrained answer is
    // the constant 1, so the spring arm contributes nothing at all until it
    // actually hits something.
    f32 limit = 1.0f;
    if (world != nullptr) {
        const f32 skin = eye_skin();
        // Cast the arm we intend to use plus the skin, so a wall sitting just
        // past the resting eye position still pulls it in.
        const VoxelHit hit = raycast_voxels(*world, anchor, dir, free_length + skin);
        if (hit) {
            // hit.distance is 0 when the anchor itself is inside solid — the
            // player's head buried in geometry someone built around them. The
            // clamp below then collapses the arm onto the head, i.e. first
            // person, which is the only framing there that is not inside a
            // wall. Fighting for a third-person shot would put the eye
            // somewhere arbitrary instead.
            limit = (hit.distance - skin) / free_length;
        }
    }
    limit = std::clamp(limit, 0.0f, 1.0f);

    // Stage three: the arm state, with deliberately asymmetric rates.
    //
    // Pulling in is instant. Easing in over even 60 ms means three or four
    // frames rendered from inside the wall, which is the exact failure being
    // fixed. What makes an instant pull-in safe here is that it moves the eye
    // *along the arm* — which is the view axis, up to follow lag — and motion
    // along the view axis is the least legible motion there is. It reads as a
    // dolly, not a cut. Nothing lateral or rotational snaps, because the
    // follow filter above is untouched by any of this.
    //
    // Extending back out is slow, and that asymmetry is what keeps the arm
    // from sawtoothing when it grazes a corner. An instant pull-in paired with
    // a *fast* ease-out is the classic version of this bug: a ray flickering
    // hit/miss frame to frame then pumps at nearly full amplitude. At a 250 ms
    // half-life the arm recovers 4.3% of the gap per 60 Hz frame, so the same
    // flicker costs a couple of centimetres of dolly instead. It cannot
    // oscillate in the resonant sense either — this is a first-order lag with
    // no overshoot term, so the worst case is a slow ratchet, never a growing
    // wobble.
    if (limit <= arm_fraction_) {
        arm_fraction_ = limit;
    } else {
        arm_fraction_ += (limit - arm_fraction_) * smoothing_factor(dt, kExtendHalfLife);
    }

    eye_ = anchor + arm * arm_fraction_;
}

glm::mat4 Camera::view() const {
    const vec3 look_at = eye_ + forward();
    return glm::lookAtLH(eye_, look_at, vec3{0.0f, 1.0f, 0.0f});
}

glm::mat4 Camera::projection() const {
    // Ask bgfx which convention the live backend wants; see make_projection.
    return make_projection(fov_y_, aspect_, near_, far_, bgfx::getCaps()->homogeneousDepth);
}

}  // namespace df
