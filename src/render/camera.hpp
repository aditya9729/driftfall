// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

#include <glm/mat4x4.hpp>

namespace df {

class VoxelWorld;

/// Builds the perspective matrix for an explicit clip-space depth convention.
///
/// A free function, and not a private detail of Camera, for one reason: this
/// exact branch shipped broken (see docs/ROADMAP.md). `Camera::projection()`
/// has to ask `bgfx::getCaps()` which convention the live backend wants, and
/// getCaps() needs an initialised backend, so the only way to unit-test the
/// choice is to lift it somewhere a test can call it with the flag set by
/// hand. `homogeneous_depth` true is the GL/WebGL2 [-1,1] range; false is the
/// [0,1] range Metal, Vulkan and D3D want.
[[nodiscard]] glm::mat4 make_projection(f32 fov_y, f32 aspect, f32 near_z, f32 far_z, bool homogeneous_depth);

/// Third-person over-the-shoulder camera with a collision spring arm.
///
/// Over-the-shoulder rather than first-person on purpose: on a phone you are
/// holding the device with the same thumbs you aim with, and seeing your own
/// silhouette is what makes cover legible. It is also the Gears framing, which
/// is the framing this game is quoting.
///
/// The cost of that framing is that the eye is metres *behind* the player, in
/// space the player can walk backwards into. The spring arm is what keeps that
/// from putting the eye inside a wall — see update() for why that is a
/// gameplay bug and not just a visual one.
class Camera {
public:
    void set_target(vec3 target) { target_ = target; }

    void set_yaw_pitch(f32 yaw, f32 pitch);

    void add_yaw_pitch(f32 delta_yaw, f32 delta_pitch);

    /// Moves the camera toward its goal and collides the arm against `world`.
    ///
    /// Smoothing is frame-rate independent so the feel does not change when
    /// the device throttles. `world` may be null, which means "no occlusion" —
    /// the arm then always runs to its full length. That is the pre-collision
    /// behaviour, kept so a camera can be driven without a sector present.
    ///
    /// TODO(integration): the `= nullptr` default is a bridge, not a design.
    /// It exists only so App keeps compiling while its call site is updated to
    /// `update(dt, &sim_->world())`; with the default in place a caller that
    /// forgets the world silently gets a camera that clips through cover.
    /// Delete the default once app.cpp passes the world.
    void update(f32 dt, const VoxelWorld* world = nullptr);

    void set_viewport(i32 width, i32 height);

    [[nodiscard]] glm::mat4 view() const;

    [[nodiscard]] glm::mat4 projection() const;

    [[nodiscard]] vec3 eye() const { return eye_; }

    /// The point the arm hangs off: the player's head, offset to the shoulder.
    ///
    /// This, not eye(), is where a shot should start. The eye is up to
    /// distance() behind it and — once the arm is pulled in by cover — can be
    /// touching or inside geometry, and `raycast_voxels` correctly reports an
    /// immediate hit from inside a solid voxel. Firing from the eye therefore
    /// means backing into cover makes every shot hit that cover at zero range.
    /// eye() and head() differ only along the arm, which is (up to smoothing
    /// lag) the view axis, so a ray from head() down forward() is the same
    /// line as the crosshair ray, just starting further along it.
    [[nodiscard]] vec3 head() const;

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

    [[nodiscard]] f32 yaw() const { return yaw_; }

    [[nodiscard]] f32 pitch() const { return pitch_; }

    /// Resting arm length: how far behind head() the eye sits with nothing in
    /// the way.
    [[nodiscard]] f32 distance() const { return distance_; }

    /// Current distance from head() to eye(), after collision. Settles at
    /// distance() in the open.
    [[nodiscard]] f32 arm_length() const;

    /// How much of the uncollided arm survives, in [0, 1]. This, and not an
    /// absolute length, is the collision state — see update().
    [[nodiscard]] f32 arm_fraction() const { return arm_fraction_; }

    /// Half-diagonal of the near plane, in voxels: how far off the view axis
    /// the frustum already reaches at its nearest point.
    [[nodiscard]] f32 near_plane_radius() const;

    /// Clearance kept between the eye and whatever the arm ran into.
    ///
    /// Sized off the near plane rather than picked: a wall the eye is exactly
    /// on is a wall the near plane's corners are already through, and the
    /// symptom is seeing into solid geometry at the screen edges. The margin
    /// on top is for the surface being hit at an angle — a truly grazing hit
    /// would need radius/cos(theta), which is unbounded, so this is a
    /// deliberate approximation rather than a proof.
    [[nodiscard]] f32 eye_skin() const;

    static constexpr f32 kMinPitch = -1.35f;
    static constexpr f32 kMaxPitch = 1.25f;

    /// How fast the eye chases its goal. 60 ms half-life is the tuned follow
    /// feel and the spring arm deliberately does not change it.
    static constexpr f32 kFollowHalfLife = 0.06f;

    /// How fast the arm gets *longer* again once cover clears. Four times the
    /// follow half-life on purpose — see update().
    static constexpr f32 kExtendHalfLife = 0.25f;

private:
    vec3 target_{0.0f};
    vec3 eye_{0.0f, 4.0f, 8.0f};

    /// Where the eye would be with no geometry in the way. This, not eye_, is
    /// the smoothing state: collision is a clamp applied to the *output* of
    /// the smoothing, so a wall can never perturb the follow filter and the
    /// filter can never delay a pull-in.
    vec3 free_eye_{0.0f, 4.0f, 8.0f};

    f32 yaw_ = 0.0f;
    f32 pitch_ = -0.25f;
    f32 distance_ = 5.5f;

    /// Fraction of the uncollided arm the eye is allowed to sit at, in [0, 1].
    /// 1 is "nothing in the way".
    f32 arm_fraction_ = 1.0f;

    /// Over the right shoulder. x is lateral along flat_right(), y is world
    /// up from the target; z is unused — the along-view offset is distance_.
    vec3 shoulder_offset_{0.9f, 1.7f, 0.0f};

    f32 aspect_ = 16.0f / 9.0f;
    f32 fov_y_ = 1.13f;  // ~65 degrees
    f32 near_ = 0.1f;
    f32 far_ = 320.0f;

    /// Extra clearance beyond the near-plane radius, in voxels (2.5 cm at the
    /// locked 0.5 m voxel). Small enough not to show as a framing change when
    /// the arm is pinned, large enough to absorb the float error in resolving
    /// a hit distance against a voxel plane.
    static constexpr f32 kSkinMargin = 0.05f;
};

}  // namespace df
