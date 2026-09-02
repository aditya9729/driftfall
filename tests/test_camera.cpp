// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "render/camera.hpp"
#include "voxel/voxel_world.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

using namespace df;

namespace {

/// One 60 Hz frame. The camera is frame-rate independent by construction, so
/// the tests only need one rate; the exception is checked explicitly below.
constexpr f32 kStep = 1.0f / 60.0f;

/// Long enough that both filters have converged: 4 s is ~66 follow half-lives
/// and 16 extension half-lives, which puts the residual below f32 resolution.
constexpr f32 kSettle = 4.0f;

void run(Camera& camera, const VoxelWorld* world, f32 seconds) {
    const int frames = static_cast<int>(seconds / kStep);
    for (int i = 0; i < frames; ++i) camera.update(kStep, world);
}

/// doctest::Approx takes a double, and -Wdouble-promotion makes an implicit
/// f32 -> f64 conversion an error, so every comparison goes through here.
doctest::Approx approx(f32 value, f64 epsilon = 1.0e-4) {
    return doctest::Approx(static_cast<f64>(value)).epsilon(epsilon);
}

void check_vec(vec3 actual, vec3 expected, f64 epsilon = 1.0e-4) {
    CHECK(static_cast<f64>(actual.x) == approx(expected.x, epsilon));
    CHECK(static_cast<f64>(actual.y) == approx(expected.y, epsilon));
    CHECK(static_cast<f64>(actual.z) == approx(expected.z, epsilon));
}

bool inside_solid(const VoxelWorld& world, vec3 p) {
    const ivec3 voxel{static_cast<i32>(std::floor(p.x)),
                      static_cast<i32>(std::floor(p.y)),
                      static_cast<i32>(std::floor(p.z))};
    return is_solid(world.at(voxel));
}

/// A camera looking down +z from the middle of the sector, level, with both
/// filters already settled. This is the fixture every spring-arm test starts
/// from: yaw 0 and pitch 0 make the arm run along exactly -z, so the expected
/// hit distances are readable by hand.
Camera settled_camera(vec3 target, const VoxelWorld* world) {
    Camera camera;
    camera.set_viewport(1920, 1080);
    camera.set_yaw_pitch(0.0f, 0.0f);
    camera.set_target(target);
    run(camera, world, kSettle);
    return camera;
}

VoxelWorld empty_world() {
    return VoxelWorld(1, 1, 1);
}

/// A wall the camera backs into: three voxels thick, with its near face at
/// z = plane_z + 1. Thick rather than a single layer so that an uncollided
/// eye ends up *inside* it, which is the failure being reproduced, rather
/// than passing clean through to the far side.
VoxelWorld wall_world(i32 plane_z) {
    VoxelWorld world(1, 1, 1);
    const ivec3 extent = world.size_in_voxels();
    for (i32 z = plane_z - 2; z <= plane_z; ++z) {
        for (i32 y = 0; y < extent.y; ++y) {
            for (i32 x = 0; x < extent.x; ++x) world.set(x, y, z, Voxel::HullPlate);
        }
    }
    return world;
}

}  // namespace

// ---------------------------------------------------------------------------
// Look limits
// ---------------------------------------------------------------------------

TEST_CASE("pitch is clamped to the look limits and yaw is not") {
    Camera camera;

    camera.set_yaw_pitch(0.0f, 0.4f);
    CHECK(static_cast<f64>(camera.pitch()) == approx(0.4f));

    camera.set_yaw_pitch(0.0f, 9.0f);
    CHECK(static_cast<f64>(camera.pitch()) == approx(Camera::kMaxPitch));

    camera.set_yaw_pitch(0.0f, -9.0f);
    CHECK(static_cast<f64>(camera.pitch()) == approx(Camera::kMinPitch));

    // Yaw wraps forever; only the sines and cosines of it are ever used.
    camera.set_yaw_pitch(100.0f, 0.0f);
    CHECK(static_cast<f64>(camera.yaw()) == approx(100.0f));
}

TEST_CASE("the pitch clamp does not wind up") {
    // The bug this guards: storing the unclamped pitch and clamping only on
    // read. Look up for a second against the limit and looking back down then
    // does nothing until the accumulated overshoot is paid off.
    Camera camera;
    camera.set_yaw_pitch(0.0f, 0.0f);

    for (int i = 0; i < 100; ++i) camera.add_yaw_pitch(0.0f, 0.5f);
    CHECK(static_cast<f64>(camera.pitch()) == approx(Camera::kMaxPitch));

    camera.add_yaw_pitch(0.0f, -0.1f);
    CHECK(static_cast<f64>(camera.pitch()) == approx(Camera::kMaxPitch - 0.1f));
}

TEST_CASE("add_yaw_pitch accumulates both axes") {
    Camera camera;
    camera.set_yaw_pitch(1.0f, 0.0f);
    camera.add_yaw_pitch(0.25f, 0.125f);
    CHECK(static_cast<f64>(camera.yaw()) == approx(1.25f));
    CHECK(static_cast<f64>(camera.pitch()) == approx(0.125f));
}

// ---------------------------------------------------------------------------
// Basis
// ---------------------------------------------------------------------------

TEST_CASE("forward, right and up are orthonormal and right-handed") {
    const f32 yaws[] = {0.0f, 0.7f, 2.5f, -1.9f, 6.0f};
    const f32 pitches[] = {0.0f, 0.6f, -0.9f, Camera::kMaxPitch, Camera::kMinPitch};

    Camera camera;
    for (const f32 yaw : yaws) {
        for (const f32 pitch : pitches) {
            camera.set_yaw_pitch(yaw, pitch);
            const vec3 f = camera.forward();
            const vec3 r = camera.right();
            const vec3 u = camera.up();

            CHECK(static_cast<f64>(glm::length(f)) == approx(1.0f));
            CHECK(static_cast<f64>(glm::length(r)) == approx(1.0f));
            CHECK(static_cast<f64>(glm::length(u)) == approx(1.0f));

            CHECK(static_cast<f64>(glm::dot(f, r)) == approx(0.0f));
            CHECK(static_cast<f64>(glm::dot(f, u)) == approx(0.0f));
            CHECK(static_cast<f64>(glm::dot(r, u)) == approx(0.0f));

            // right x up == forward is the right-handed triple. The *view* is
            // left-handed (+z into the screen); the three world-space vectors
            // are still a right-handed set, and the sky pass depends on it.
            check_vec(glm::cross(r, u), f);
        }
    }
}

TEST_CASE("the basis is the one view() actually uses") {
    // The header promises these vectors match view(); the sky pass builds its
    // rays from them instead of inverting the view-projection. If they ever
    // drift apart the sky tilts and nothing else does.
    Camera camera;
    camera.set_yaw_pitch(1.1f, -0.4f);
    camera.set_target(vec3{12.0f, 3.0f, 20.0f});
    run(camera, nullptr, kSettle);

    const glm::mat4 view = camera.view();
    check_vec(vec3(view * glm::vec4(camera.forward(), 0.0f)), vec3{0.0f, 0.0f, 1.0f}, 1.0e-3);
    check_vec(vec3(view * glm::vec4(camera.right(), 0.0f)), vec3{1.0f, 0.0f, 0.0f}, 1.0e-3);
    check_vec(vec3(view * glm::vec4(camera.up(), 0.0f)), vec3{0.0f, 1.0f, 0.0f}, 1.0e-3);
    check_vec(vec3(view * glm::vec4(camera.eye(), 1.0f)), vec3{0.0f}, 1.0e-3);
}

TEST_CASE("the flat axes are horizontal and ignore pitch") {
    Camera camera;

    for (const f32 pitch : {0.0f, 0.9f, -1.2f, Camera::kMaxPitch}) {
        camera.set_yaw_pitch(2.2f, pitch);
        const vec3 ff = camera.flat_forward();
        const vec3 fr = camera.flat_right();

        CHECK(static_cast<f64>(ff.y) == approx(0.0f));
        CHECK(static_cast<f64>(fr.y) == approx(0.0f));
        CHECK(static_cast<f64>(glm::length(ff)) == approx(1.0f));
        CHECK(static_cast<f64>(glm::length(fr)) == approx(1.0f));
        CHECK(static_cast<f64>(glm::dot(ff, fr)) == approx(0.0f));
    }

    // Pitching must not change them at all: that is the whole point — walking
    // forward while looking at the ceiling still walks forward.
    camera.set_yaw_pitch(2.2f, 0.0f);
    const vec3 level_forward = camera.flat_forward();
    const vec3 level_right = camera.flat_right();
    camera.set_yaw_pitch(2.2f, -1.3f);
    check_vec(camera.flat_forward(), level_forward);
    check_vec(camera.flat_right(), level_right);
}

TEST_CASE("at zero pitch the flat axes are the full basis") {
    Camera camera;
    camera.set_yaw_pitch(-0.8f, 0.0f);
    check_vec(camera.flat_forward(), camera.forward());
    check_vec(camera.flat_right(), camera.right());
}

TEST_CASE("flat_forward is the horizontal projection of forward") {
    Camera camera;
    camera.set_yaw_pitch(0.6f, 0.5f);
    const vec3 f = camera.forward();
    check_vec(camera.flat_forward(), glm::normalize(vec3{f.x, 0.0f, f.z}));
}

// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------

TEST_CASE("set_viewport derives the aspect ratio") {
    Camera camera;
    camera.set_viewport(1920, 1080);
    CHECK(static_cast<f64>(camera.aspect()) == approx(1920.0f / 1080.0f));

    // Portrait: this is a phone game, so a taller-than-wide viewport is not an
    // edge case.
    camera.set_viewport(1080, 1920);
    CHECK(static_cast<f64>(camera.aspect()) == approx(1080.0f / 1920.0f));
}

TEST_CASE("a zero or negative height leaves the aspect alone") {
    // Minimising a window reports a zero height on every desktop backend. The
    // guard is what keeps a division by zero out of the projection matrix,
    // which would otherwise persist as NaN long after the window came back.
    Camera camera;
    camera.set_viewport(1920, 1080);
    const f32 good = camera.aspect();

    camera.set_viewport(1920, 0);
    CHECK(static_cast<f64>(camera.aspect()) == approx(good));

    camera.set_viewport(1920, -4);
    CHECK(static_cast<f64>(camera.aspect()) == approx(good));

    CHECK(std::isfinite(camera.aspect()));
}

// ---------------------------------------------------------------------------
// Clip-space depth convention
//
// This is the bug recorded at docs/ROADMAP.md:47 — the projection hardcoded
// one convention and the GL backends rendered nothing, silently. It is pure
// math, so make_projection() takes the flag as a parameter and these tests can
// exercise both branches without an initialised bgfx device.
// ---------------------------------------------------------------------------

TEST_CASE("the zero-to-one convention maps near to 0 and far to 1") {
    const glm::mat4 p = make_projection(1.13f, 16.0f / 9.0f, 0.1f, 320.0f, false);

    const glm::vec4 at_near = p * glm::vec4(0.0f, 0.0f, 0.1f, 1.0f);
    const glm::vec4 at_far = p * glm::vec4(0.0f, 0.0f, 320.0f, 1.0f);
    CHECK(static_cast<f64>(at_near.z / at_near.w) == approx(0.0f, 1.0e-5));
    CHECK(static_cast<f64>(at_far.z / at_far.w) == approx(1.0f, 1.0e-5));
}

TEST_CASE("the homogeneous convention maps near to -1 and far to +1") {
    const glm::mat4 p = make_projection(1.13f, 16.0f / 9.0f, 0.1f, 320.0f, true);

    const glm::vec4 at_near = p * glm::vec4(0.0f, 0.0f, 0.1f, 1.0f);
    const glm::vec4 at_far = p * glm::vec4(0.0f, 0.0f, 320.0f, 1.0f);
    CHECK(static_cast<f64>(at_near.z / at_near.w) == approx(-1.0f, 1.0e-5));
    CHECK(static_cast<f64>(at_far.z / at_far.w) == approx(1.0f, 1.0e-5));
}

TEST_CASE("the two conventions differ only in depth") {
    const glm::mat4 zo = make_projection(1.13f, 16.0f / 9.0f, 0.1f, 320.0f, false);
    const glm::mat4 no = make_projection(1.13f, 16.0f / 9.0f, 0.1f, 320.0f, true);

    const glm::vec4 point(3.0f, -2.0f, 40.0f, 1.0f);
    const glm::vec4 a = zo * point;
    const glm::vec4 b = no * point;
    CHECK(static_cast<f64>(a.x / a.w) == approx(b.x / b.w));
    CHECK(static_cast<f64>(a.y / a.w) == approx(b.y / b.w));
    // ...and the wrong one puts the near plane outside the range the backend
    // clips against, which is why the symptom was an empty screen.
    CHECK(static_cast<f64>(a.z / a.w) != approx(b.z / b.w));
}

TEST_CASE("the projection is left-handed: +z in view space is in front") {
    const glm::mat4 p = make_projection(1.13f, 16.0f / 9.0f, 0.1f, 320.0f, false);
    const glm::vec4 in_front = p * glm::vec4(0.0f, 0.0f, 10.0f, 1.0f);
    CHECK(in_front.w > 0.0f);
}

// ---------------------------------------------------------------------------
// Spring arm
// ---------------------------------------------------------------------------

TEST_CASE("the skin clears the near plane") {
    // A wall the eye sits exactly on is a wall the near plane's corners are
    // already through, and the symptom is seeing into solid geometry at the
    // screen edges. The skin is sized off that radius, not picked.
    Camera camera;
    camera.set_viewport(1920, 1080);
    CHECK(camera.eye_skin() > camera.near_plane_radius());

    // Widening the viewport widens the near plane, so the clearance follows.
    const f32 wide = camera.near_plane_radius();
    camera.set_viewport(1080, 1920);
    CHECK(camera.near_plane_radius() < wide);
}

TEST_CASE("with nothing behind the player the arm runs to full distance") {
    const VoxelWorld world = empty_world();
    const vec3 target{16.0f, 6.0f, 16.0f};
    const Camera camera = settled_camera(target, &world);

    CHECK(static_cast<f64>(camera.arm_fraction()) == approx(1.0f));
    CHECK(static_cast<f64>(camera.arm_length()) == approx(camera.distance(), 1.0e-3));
    // Level and facing +z, so the eye is straight back along -z from the head.
    check_vec(camera.eye(), camera.head() - camera.forward() * camera.distance(), 1.0e-3);
}

TEST_CASE("the head sits at the shoulder, not at the target") {
    // The arm pivot is what a shot should come from, so its offset from the
    // player origin is load-bearing, not decoration.
    Camera camera;
    camera.set_yaw_pitch(0.0f, 0.0f);
    camera.set_target(vec3{16.0f, 6.0f, 16.0f});

    const vec3 head = camera.head();
    CHECK(head.y > 6.0f);                              // raised toward the head
    CHECK(static_cast<f64>(head.z) == approx(16.0f));  // no offset along view
    CHECK(head.x > 16.0f);                             // offset along flat_right, +x at yaw 0
}

TEST_CASE("a wall behind the player pulls the eye in") {
    // The whole point: without this the eye ends up inside the wall, and
    // raycast_voxels correctly reports an immediate hit from inside a solid
    // voxel, so every shot lands on the cover you are standing against.
    const VoxelWorld world = wall_world(12);  // face at z = 13
    const vec3 target{16.0f, 6.0f, 16.0f};
    const Camera camera = settled_camera(target, &world);

    const f32 to_wall = 3.0f;  // head z = 16, wall face z = 13
    CHECK(static_cast<f64>(camera.arm_length()) == approx(to_wall - camera.eye_skin(), 1.0e-3));
    CHECK(camera.arm_length() < camera.distance());
    CHECK_FALSE(inside_solid(world, camera.eye()));
}

TEST_CASE("the pulled-in eye keeps its clearance from the surface") {
    const VoxelWorld world = wall_world(12);
    const Camera camera = settled_camera(vec3{16.0f, 6.0f, 16.0f}, &world);

    const f32 gap = camera.eye().z - 13.0f;  // wall face is at z = 13
    CHECK(gap > 0.0f);
    CHECK(static_cast<f64>(gap) == approx(camera.eye_skin(), 1.0e-3));
}

TEST_CASE("a null world means no occlusion at all") {
    // The pre-collision behaviour, kept deliberately: a camera driven without
    // a sector runs its arm out to full length rather than refusing to move.
    const VoxelWorld world = wall_world(12);
    Camera camera = settled_camera(vec3{16.0f, 6.0f, 16.0f}, nullptr);

    CHECK(static_cast<f64>(camera.arm_length()) == approx(camera.distance(), 1.0e-3));
    // ...which is exactly the shipped bug when the world is not passed: the
    // eye ends up buried in the cover the player is standing against, and a
    // shot fired from it hits that cover at zero range.
    CHECK(inside_solid(world, camera.eye()));
}

TEST_CASE("the eye is pulled in within a single frame") {
    // Easing in would render three or four frames from inside the wall. A wall
    // that appears — someone builds a barricade behind you, or you back into
    // one — has to be handled on the frame it appears.
    const VoxelWorld open = empty_world();
    const VoxelWorld wall = wall_world(12);

    Camera camera = settled_camera(vec3{16.0f, 6.0f, 16.0f}, &open);
    REQUIRE(camera.arm_length() > 5.0f);

    camera.update(kStep, &wall);
    CHECK(static_cast<f64>(camera.arm_length()) == approx(3.0f - camera.eye_skin(), 1.0e-3));
    CHECK_FALSE(inside_solid(wall, camera.eye()));
}

TEST_CASE("the arm extends back out slowly when cover clears") {
    const VoxelWorld open = empty_world();
    const VoxelWorld wall = wall_world(12);

    Camera camera = settled_camera(vec3{16.0f, 6.0f, 16.0f}, &wall);
    const f32 pinned = camera.arm_length();
    const f32 gap = camera.distance() - pinned;
    REQUIRE(gap > 2.0f);

    // One frame of release must recover only a sliver of the gap. This is the
    // asymmetry that stops a grazing corner sawtoothing the arm.
    camera.update(kStep, &open);
    const f32 after_one = camera.arm_length();
    CHECK(after_one > pinned);
    CHECK(after_one - pinned < 0.1f * gap);

    // ...but it does get all the way back, and never overshoots past full.
    run(camera, &open, 3.0f);
    CHECK(static_cast<f64>(camera.arm_length()) == approx(camera.distance(), 1.0e-3));
    CHECK(camera.arm_fraction() <= 1.0f);
}

TEST_CASE("the arm does not drift once settled") {
    const VoxelWorld world = wall_world(12);
    Camera camera = settled_camera(vec3{16.0f, 6.0f, 16.0f}, &world);

    const vec3 first = camera.eye();
    f32 worst = 0.0f;
    for (int i = 0; i < 120; ++i) {
        camera.update(kStep, &world);
        worst = std::max(worst, glm::length(camera.eye() - first));
    }
    CHECK(worst < 1.0e-4f);
}

TEST_CASE("a flickering hit does not sawtooth the arm") {
    // The failure this design is built to avoid. A ray grazing a corner
    // alternates hit/miss frame to frame; with a fast ease-out the arm pumps
    // the full length of the gap every other frame. Here it is bounded by one
    // frame's worth of extension, because pulling in is instant and letting
    // out is four times slower than the follow filter.
    const VoxelWorld open = empty_world();
    const VoxelWorld wall = wall_world(12);

    Camera camera = settled_camera(vec3{16.0f, 6.0f, 16.0f}, &wall);
    const f32 gap = camera.distance() - camera.arm_length();

    f32 lo = camera.arm_length();
    f32 hi = camera.arm_length();
    for (int i = 0; i < 120; ++i) {
        camera.update(kStep, (i % 2 == 0) ? &open : &wall);
        lo = std::min(lo, camera.arm_length());
        hi = std::max(hi, camera.arm_length());
    }
    // A symmetric-rate implementation swings the whole gap here.
    CHECK(hi - lo < 0.1f * gap);
    CHECK_FALSE(inside_solid(wall, camera.eye()));
}

TEST_CASE("a tight corridor still gives a usable camera") {
    // Solid rock with a 3-wide, 4-tall corridor cut through it, and the player
    // facing across the corridor rather than along it — the worst case for an
    // over-the-shoulder arm that is 5.5 voxels long in a 1.5 m gap.
    VoxelWorld world(1, 1, 1);
    const ivec3 extent = world.size_in_voxels();
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 y = 0; y < extent.y; ++y) {
            for (i32 x = 0; x < extent.x; ++x) world.set(x, y, z, Voxel::Bulkhead);
        }
    }
    for (i32 z = 15; z <= 17; ++z) {
        for (i32 y = 1; y <= 4; ++y) {
            for (i32 x = 2; x <= 29; ++x) world.set(x, y, z, Voxel::Empty);
        }
    }

    // Head lands at (16.9, 2.7, 16.5): inside the corridor, facing +z, so the
    // arm runs into the z = 15 wall face 1.5 voxels back.
    const Camera camera = settled_camera(vec3{16.0f, 1.0f, 16.5f}, &world);

    REQUIRE_FALSE(inside_solid(world, camera.head()));
    CHECK(static_cast<f64>(camera.arm_length()) == approx(1.5f - camera.eye_skin(), 1.0e-3));
    CHECK(camera.arm_length() > 0.0f);
    CHECK(camera.arm_length() < camera.distance());
    CHECK_FALSE(inside_solid(world, camera.eye()));
    CHECK(std::isfinite(camera.eye().x));
    CHECK(std::isfinite(camera.eye().y));
    CHECK(std::isfinite(camera.eye().z));
}

TEST_CASE("a buried head collapses the arm to first person") {
    // Someone builds a barricade into your face. There is no third-person
    // framing here that is not inside a wall, so the arm gives up rather than
    // putting the eye somewhere arbitrary.
    VoxelWorld world(1, 1, 1);
    const ivec3 extent = world.size_in_voxels();
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 y = 0; y < extent.y; ++y) {
            for (i32 x = 0; x < extent.x; ++x) world.set(x, y, z, Voxel::Barricade);
        }
    }

    const Camera camera = settled_camera(vec3{16.0f, 6.0f, 16.0f}, &world);
    CHECK(static_cast<f64>(camera.arm_fraction()) == approx(0.0f));
    check_vec(camera.eye(), camera.head());
}

TEST_CASE("the shot line from the head is the crosshair line") {
    // The reason app.cpp should raycast from head() rather than eye(): the two
    // differ only *along* the arm, so a ray from the head down forward() is
    // the same line the crosshair sits on, started further along it. It has to
    // hold whether or not the arm is pinned.
    const VoxelWorld open = empty_world();
    const VoxelWorld wall = wall_world(12);

    for (const VoxelWorld* world : {&open, &wall}) {
        Camera camera;
        camera.set_viewport(1920, 1080);
        camera.set_yaw_pitch(0.9f, -0.3f);
        camera.set_target(vec3{16.0f, 6.0f, 16.0f});
        run(camera, world, kSettle);

        const vec3 offset = camera.head() - camera.eye();
        REQUIRE(glm::length(offset) > 0.1f);
        check_vec(glm::normalize(offset), camera.forward(), 1.0e-3);
    }
}

TEST_CASE("the follow filter is frame-rate independent") {
    // The reason the smoothing is written against dt at all: a device that
    // throttles to 30 Hz must not feel like a different camera.
    const VoxelWorld world = empty_world();

    Camera fast = settled_camera(vec3{16.0f, 6.0f, 16.0f}, &world);
    Camera slow = fast;

    // Same 0.25 s of travel, four times the step.
    fast.set_target(vec3{22.0f, 6.0f, 19.0f});
    slow.set_target(vec3{22.0f, 6.0f, 19.0f});
    for (int i = 0; i < 60; ++i) fast.update(1.0f / 240.0f, &world);
    for (int i = 0; i < 15; ++i) slow.update(1.0f / 60.0f, &world);

    check_vec(fast.eye(), slow.eye(), 1.0e-3);
}

TEST_CASE("a zero-length step leaves the camera where it is") {
    // handle_resize and the first frame after a stall can both produce dt 0.
    const VoxelWorld world = empty_world();
    Camera camera = settled_camera(vec3{16.0f, 6.0f, 16.0f}, &world);

    const vec3 before = camera.eye();
    camera.update(0.0f, &world);
    check_vec(camera.eye(), before);
}
