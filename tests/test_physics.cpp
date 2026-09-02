// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/clock.hpp"
#include "game/physics.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace df;

namespace {

/// 1 voxel = 0.5 m, so this is a 0.5 m x 1.8 m x 0.5 m body.
constexpr vec3 kPlayer{0.5f, 1.8f, 0.5f};

constexpr f32 kDt = static_cast<f32>(FixedTimestep::kStep);

/// One step of walking at the client's kMoveSpeed = 11 voxels/s.
constexpr f32 kWalkStep = 11.0f * kDt;

/// Rate-limited ejection budget for one step: 12 voxels/s, which clears a
/// one-voxel overlap in five steps.
constexpr f32 kEjectStep = 12.0f * kDt;

/// Centre height of a settled body standing on a deck whose top face is y = 1.
constexpr f32 kRestY = 1.0f + kContactSkin + 1.8f;

f32 abs32(f32 v) {
    return v < 0.0f ? -v : v;
}

/// A 32^3 sector with a solid deck at y = 0 and nothing else.
VoxelWorld deck_world() {
    VoxelWorld world(1, 1, 1);
    for (i32 x = 0; x < 32; ++x) {
        for (i32 z = 0; z < 32; ++z) world.set(x, 0, z, Voxel::Bulkhead);
    }
    return world;
}

void fill_box(VoxelWorld& world, ivec3 lo, ivec3 hi, Voxel v) {
    for (i32 x = lo.x; x <= hi.x; ++x) {
        for (i32 y = lo.y; y <= hi.y; ++y) {
            for (i32 z = lo.z; z <= hi.z; ++z) world.set(x, y, z, v);
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Walking, landing, contacts
// ---------------------------------------------------------------------------

TEST_CASE("walking across a flat deck touches nothing and stays grounded") {
    const VoxelWorld world = deck_world();
    const SweepResult r = move_and_slide(
        world, vec3{8.0f, kRestY, 8.0f}, kPlayer, vec3{kWalkStep, 0.0f, 0.0f}, vec3{11.0f, 0.0f, 0.0f});

    CHECK_FALSE(r.hit_x);
    CHECK_FALSE(r.hit_y);
    CHECK_FALSE(r.hit_z);
    CHECK(r.grounded);
    CHECK_FALSE(r.out_of_bounds);
    CHECK(static_cast<f64>(r.position.x) == doctest::Approx(static_cast<f64>(8.0f + kWalkStep)));
    CHECK(static_cast<f64>(r.velocity.x) == doctest::Approx(11.0));
}

TEST_CASE("gravity does not accumulate while standing on the deck") {
    // The bug this pins: if the blocked axis is not zeroed in the returned
    // velocity, downward speed keeps integrating while the player stands
    // still, and the first step off a ledge drops them at terminal velocity.
    const VoxelWorld world = deck_world();
    constexpr f32 kGravity = -40.0f;

    vec3 position{8.0f, kRestY, 8.0f};
    vec3 velocity{0.0f, 0.0f, 0.0f};
    for (int step = 0; step < 300; ++step) {
        velocity.y += kGravity * kDt;
        const SweepResult r = move_and_slide(world, position, kPlayer, velocity * kDt, velocity);
        position = r.position;
        velocity = r.velocity;
        REQUIRE(r.grounded);
    }

    // One step of gravity, zeroed again every step. Never 300 steps of it.
    CHECK(velocity.y == 0.0f);
    CHECK(static_cast<f64>(position.y) == doctest::Approx(static_cast<f64>(kRestY)).epsilon(0.001));
}

TEST_CASE("a resting body is bit-stable across steps") {
    // Any drift here becomes a body that slowly sinks or jitters over a
    // ten-minute run, and it breaks replay determinism outright.
    const VoxelWorld world = deck_world();
    vec3 position{8.0f, kRestY, 8.0f};

    const SweepResult settle =
        move_and_slide(world, position, kPlayer, vec3{0.0f, -0.02f, 0.0f}, vec3{0.0f, -1.2f, 0.0f});
    position = settle.position;

    for (int step = 0; step < 64; ++step) {
        const SweepResult r =
            move_and_slide(world, position, kPlayer, vec3{0.0f, -0.02f, 0.0f}, vec3{0.0f, -1.2f, 0.0f});
        CHECK(r.position.y == position.y);
        CHECK(r.hit_y);
        CHECK(r.velocity.y == 0.0f);
        position = r.position;
    }
}

TEST_CASE("a falling body lands on the deck and stops") {
    const VoxelWorld world = deck_world();
    vec3 position{8.0f, 20.0f, 8.0f};
    vec3 velocity{0.0f, -30.0f, 0.0f};

    bool landed = false;
    for (int step = 0; step < 200 && !landed; ++step) {
        const SweepResult r = move_and_slide(world, position, kPlayer, vec3{0.0f, -0.5f, 0.0f}, velocity);
        position = r.position;
        velocity = r.velocity;
        landed = r.hit_y;
        if (landed) {
            CHECK(r.grounded);
            CHECK(r.velocity.y == 0.0f);
        }
    }

    REQUIRE(landed);
    // Feet on the deck's top face, one skin above it. Never below.
    CHECK(static_cast<f64>(position.y - kPlayer.y) == doctest::Approx(1.0).epsilon(0.001));
    CHECK(position.y - kPlayer.y >= 1.0f);
}

TEST_CASE("a jump into a ceiling bonks and drops the upward velocity") {
    VoxelWorld world = deck_world();
    fill_box(world, {0, 12, 0}, {31, 12, 31}, Voxel::HullPlate);

    // Head just under the ceiling's underside at y = 12.
    const vec3 position{8.0f, 12.0f - 0.4f - kPlayer.y, 8.0f};
    const SweepResult r =
        move_and_slide(world, position, kPlayer, vec3{0.0f, 0.9f, 0.0f}, vec3{0.0f, 20.0f, 0.0f});

    CHECK(r.hit_y);
    CHECK(r.velocity.y == 0.0f);
    CHECK_FALSE(r.grounded);
    CHECK(static_cast<f64>(r.position.y + kPlayer.y) == doctest::Approx(12.0).epsilon(0.001));
    CHECK(r.position.y + kPlayer.y <= 12.0f);
}

// ---------------------------------------------------------------------------
// Sliding
// ---------------------------------------------------------------------------

TEST_CASE("a diagonal move into a wall slides along it") {
    // Sticking to a wall instead of sliding is the single most-felt physics
    // defect in a shooter, so this asserts the tangential axis is untouched.
    VoxelWorld world = deck_world();
    fill_box(world, {10, 1, 0}, {10, 8, 31}, Voxel::HullPlate);

    const vec3 position{9.0f, kRestY, 8.0f};
    const SweepResult r =
        move_and_slide(world, position, kPlayer, vec3{0.6f, 0.0f, 0.6f}, vec3{11.0f, 0.0f, 11.0f});

    CHECK(r.hit_x);
    CHECK_FALSE(r.hit_z);
    CHECK(r.velocity.x == 0.0f);
    CHECK(r.velocity.z == 11.0f);  // tangential speed preserved exactly
    CHECK(static_cast<f64>(r.position.z) == doctest::Approx(8.6));
    CHECK(static_cast<f64>(r.position.x + kPlayer.x) == doctest::Approx(10.0).epsilon(0.001));
    CHECK(r.position.x + kPlayer.x <= 10.0f);
}

TEST_CASE("an inside corner blocks both horizontal axes but not the vertical") {
    VoxelWorld world = deck_world();
    fill_box(world, {10, 1, 0}, {10, 8, 31}, Voxel::HullPlate);
    fill_box(world, {0, 1, 10}, {31, 8, 10}, Voxel::HullPlate);

    const SweepResult r = move_and_slide(
        world, vec3{9.0f, 6.0f, 9.0f}, kPlayer, vec3{0.6f, -0.6f, 0.6f}, vec3{11.0f, -9.0f, 11.0f});

    CHECK(r.hit_x);
    CHECK(r.hit_z);
    CHECK_FALSE(r.hit_y);
    CHECK(r.velocity.x == 0.0f);
    CHECK(r.velocity.z == 0.0f);
    CHECK(r.velocity.y == -9.0f);
    CHECK(static_cast<f64>(r.position.y) == doctest::Approx(5.4));
}

TEST_CASE("a body fits through a four-voxel doorway without catching") {
    // The scale decision under test: a 1.0 x 3.6 x 1.0 voxel body needs a
    // 4-voxel opening. A 3-voxel door is impassable for it.
    VoxelWorld world = deck_world();
    fill_box(world, {0, 1, 10}, {31, 8, 10}, Voxel::HullPlate);
    fill_box(world, {12, 1, 10}, {15, 4, 10}, Voxel::Empty);

    vec3 position{14.0f, kRestY, 8.0f};
    for (int step = 0; step < 40; ++step) {
        const SweepResult r =
            move_and_slide(world, position, kPlayer, vec3{0.0f, 0.0f, kWalkStep}, vec3{0.0f, 0.0f, 11.0f});
        REQUIRE_FALSE(r.hit_z);
        REQUIRE_FALSE(r.hit_y);
        position = r.position;
    }
    CHECK(position.z > 11.0f);  // through the door, not stopped in it
}

// ---------------------------------------------------------------------------
// Exact voxel-boundary straddles
// ---------------------------------------------------------------------------

TEST_CASE("a face exactly on a voxel plane is touching, not penetrating") {
    VoxelWorld world = deck_world();
    fill_box(world, {10, 1, 0}, {10, 8, 31}, Voxel::HullPlate);

    // hi.x lands exactly on 10.0, the plane the solid column starts at.
    const vec3 position{10.0f - kPlayer.x, kRestY, 8.0f};
    CHECK_FALSE(box_overlaps_solid(world, position, kPlayer));

    const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kEjectStep);
    CHECK_FALSE(d.penetrating);
    CHECK_FALSE(d.sealed);
    CHECK(d.resolved);
    CHECK(d.position.x == position.x);
}

TEST_CASE("pressing into a wall you are flush against is still a contact") {
    // Travelled distance is zero, but the velocity must still be zeroed or
    // the caller keeps integrating into the wall forever.
    VoxelWorld world = deck_world();
    fill_box(world, {10, 1, 0}, {10, 8, 31}, Voxel::HullPlate);

    const vec3 position{10.0f - kPlayer.x, kRestY, 8.0f};
    const SweepResult r =
        move_and_slide(world, position, kPlayer, vec3{0.4f, 0.0f, 0.0f}, vec3{11.0f, 0.0f, 0.0f});

    CHECK(r.hit_x);
    CHECK(r.velocity.x == 0.0f);
    CHECK(r.position.x == position.x);
}

TEST_CASE("a move that lands exactly flush on a plane is not a contact") {
    // The adversarial half of the same convention: arriving at hi.x == 10.0
    // must complete the full displacement and report no hit.
    VoxelWorld world = deck_world();
    fill_box(world, {10, 1, 0}, {10, 8, 31}, Voxel::HullPlate);

    const vec3 position{9.0f, kRestY, 8.0f};  // hi.x = 9.5
    const SweepResult r =
        move_and_slide(world, position, kPlayer, vec3{0.5f, 0.0f, 0.0f}, vec3{11.0f, 0.0f, 0.0f});

    CHECK_FALSE(r.hit_x);
    CHECK(r.velocity.x == 11.0f);
    CHECK(static_cast<f64>(r.position.x + kPlayer.x) == doctest::Approx(10.0));
    CHECK_FALSE(box_overlaps_solid(world, r.position, kPlayer));
}

TEST_CASE("a body flush against a wall can still walk away from it") {
    VoxelWorld world = deck_world();
    fill_box(world, {10, 1, 0}, {10, 8, 31}, Voxel::HullPlate);

    const vec3 position{10.0f - kPlayer.x, kRestY, 8.0f};
    const SweepResult r =
        move_and_slide(world, position, kPlayer, vec3{-0.5f, 0.0f, 0.0f}, vec3{-11.0f, 0.0f, 0.0f});

    CHECK_FALSE(r.hit_x);
    CHECK(static_cast<f64>(r.position.x) == doctest::Approx(static_cast<f64>(position.x) - 0.5));
}

// ---------------------------------------------------------------------------
// De-penetration
// ---------------------------------------------------------------------------

TEST_CASE("a block placed at your feet stands you on top of it") {
    VoxelWorld world = deck_world();
    const vec3 position{8.0f, kRestY, 8.0f};
    fill_box(world, {7, 1, 7}, {9, 1, 9}, Voxel::Barricade);

    REQUIRE(box_overlaps_solid(world, position, kPlayer));
    const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kInstantEject);

    CHECK(d.penetrating);
    CHECK_FALSE(d.sealed);
    CHECK(d.resolved);
    CHECK(d.position.x == position.x);
    CHECK(d.position.z == position.z);
    CHECK(d.position.y > position.y);
    CHECK(static_cast<f64>(d.position.y - kPlayer.y) == doctest::Approx(2.0).epsilon(0.001));
    CHECK(box_grounded(world, d.position, kPlayer));
}

TEST_CASE("up wins over a cheaper sideways escape") {
    // A one-voxel pillar clipping the body: sideways is the shorter push, but
    // standing on what you placed is the outcome the player expects.
    VoxelWorld world = deck_world();
    const vec3 position{8.9f, kRestY, 8.0f};  // lo.x = 8.4, so a 0.6 push clears x = 8
    fill_box(world, {8, 1, 8}, {8, 1, 8}, Voxel::Barricade);

    REQUIRE(box_overlaps_solid(world, position, kPlayer));
    const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kInstantEject);

    CHECK(d.penetrating);
    CHECK(d.resolved);
    CHECK(d.position.x == position.x);
    CHECK(d.position.y > position.y);
}

TEST_CASE("ejection is rate-limited, so a deep burial is a shove and not a teleport") {
    VoxelWorld world = deck_world();
    fill_box(world, {0, 1, 0}, {31, 3, 31}, Voxel::HullPlate);

    vec3 position{8.0f, kRestY, 8.0f};
    int steps = 0;
    for (; steps < 200; ++steps) {
        const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kEjectStep);
        REQUIRE_FALSE(d.sealed);
        const f32 moved = abs32(d.position.y - position.y);
        CHECK(moved <= kEjectStep + 1.0e-5f);
        position = d.position;
        if (d.resolved) break;
    }

    REQUIRE(steps < 200);
    CHECK(steps > 1);  // a 3-voxel burial must not clear in a single step
    CHECK_FALSE(box_overlaps_solid(world, position, kPlayer));
    CHECK(static_cast<f64>(position.y - kPlayer.y) == doctest::Approx(4.0).epsilon(0.001));
}

TEST_CASE("a body buried by a rebuilt wall is never permanently trapped") {
    // The Warden's whole mechanic, and the bug that would otherwise ship: the
    // body is inside solid geometry, every axis reports blocked, and it can
    // never move again. Here the player also holds the stick pointing further
    // into the wall the entire time, which is what a panicking player does.
    VoxelWorld world = deck_world();
    fill_box(world, {4, 1, 8}, {12, 6, 10}, Voxel::Reinforced);  // a 3-voxel-thick wall

    vec3 position{8.0f, 3.5f, 9.0f};  // dead centre of it
    REQUIRE(box_overlaps_solid(world, position, kPlayer));

    bool escaped = false;
    int steps = 0;
    for (; steps < 300 && !escaped; ++steps) {
        const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kEjectStep);
        REQUIRE_FALSE(d.sealed);
        position = d.position;

        // Input pushing deeper into the wall must never outrun the ejection.
        const SweepResult r =
            move_and_slide(world, position, kPlayer, vec3{0.0f, 0.0f, kWalkStep}, vec3{0.0f, 0.0f, 11.0f});
        position = r.position;
        escaped = !box_overlaps_solid(world, position, kPlayer);
    }

    REQUIRE(escaped);
    CHECK(steps > 1);          // shoved out over time, not teleported
    CHECK(position.z < 9.0f);  // out the near face, never through to the far side

    // And once out, the body can actually move again.
    const SweepResult after =
        move_and_slide(world, position, kPlayer, vec3{0.0f, 0.0f, -kWalkStep}, vec3{0.0f, 0.0f, -11.0f});
    CHECK(after.position.z < position.z);
    CHECK_FALSE(after.hit_z);
}

TEST_CASE("a penetrating body can leave geometry but can never enter a new voxel") {
    // The precise invariant, which is weaker than "cannot move deeper": the
    // body may slide within the cells it already occupies, because those
    // cannot block it without welding it in place. What it can never do is
    // cross into a voxel it was not already in. That is what bounds how far
    // wrong a penetrating step can go, and it is why de-penetration only ever
    // has to undo one voxel of overlap rather than an unbounded amount.
    VoxelWorld world = deck_world();
    fill_box(world, {10, 1, 0}, {12, 8, 31}, Voxel::HullPlate);

    const vec3 start{10.2f, kRestY, 8.0f};  // straddling the wall's -x face
    REQUIRE(box_overlaps_solid(world, start, kPlayer));

    vec3 position = start;
    bool stopped = false;
    for (int step = 0; step < 16; ++step) {
        const SweepResult r =
            move_and_slide(world, position, kPlayer, vec3{0.3f, 0.0f, 0.0f}, vec3{11.0f, 0.0f, 0.0f});
        position = r.position;
        // Never past the far plane of voxel 10, which is the last one it began
        // the step inside.
        REQUIRE(position.x + kPlayer.x <= 11.0f);
        if (r.hit_x) {
            CHECK(r.velocity.x == 0.0f);
            stopped = true;
            break;
        }
    }
    CHECK(stopped);

    const SweepResult out =
        move_and_slide(world, start, kPlayer, vec3{-0.3f, 0.0f, 0.0f}, vec3{-11.0f, 0.0f, 0.0f});
    CHECK_FALSE(out.hit_x);
    CHECK(out.velocity.x == -11.0f);
    CHECK(static_cast<f64>(out.position.x) == doctest::Approx(9.9));
}

TEST_CASE("being sealed in is reported, not silently teleported out of") {
    // Entombed by the Warden. Moving the body would have to pass it through
    // the new wall, which reads as a bug and can deposit it in a sealed room.
    // The contract is: do not move, say so, let the caller crush and let the
    // player breach.
    VoxelWorld world = deck_world();
    fill_box(world, {8, 1, 8}, {24, 24, 24}, Voxel::Reinforced);

    const vec3 position{16.0f, 12.0f, 16.0f};
    REQUIRE(box_overlaps_solid(world, position, kPlayer));

    const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kInstantEject);
    CHECK(d.penetrating);
    CHECK(d.sealed);
    CHECK_FALSE(d.resolved);
    CHECK(d.position.x == position.x);
    CHECK(d.position.y == position.y);
    CHECK(d.position.z == position.z);
}

TEST_CASE("breaching out of a seal frees the body on the next step") {
    // Sealed must be a state you shoot your way out of, not an absorbing one.
    VoxelWorld world = deck_world();
    fill_box(world, {8, 1, 8}, {24, 24, 24}, Voxel::Reinforced);

    const vec3 position{16.0f, 12.0f, 16.0f};
    REQUIRE(resolve_penetration(world, position, kPlayer, kInstantEject).sealed);

    // Shoot the ceiling out: a clear column above the body's head.
    fill_box(world, {14, 14, 14}, {18, 24, 18}, Voxel::Empty);

    const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kInstantEject);
    CHECK(d.penetrating);
    CHECK_FALSE(d.sealed);
    CHECK(d.resolved);
    CHECK(d.position.y > position.y);
    CHECK_FALSE(box_overlaps_solid(world, d.position, kPlayer));
}

TEST_CASE("the push clears every overlapped cell, not merely the nearest one") {
    // Two solids at different depths under the body. Clearing only the nearest
    // leaves it inside the other, and the next step has to start over.
    VoxelWorld world = deck_world();
    const vec3 position{8.0f, kRestY, 8.0f};
    world.set(7, 1, 7, Voxel::Barricade);
    world.set(8, 3, 8, Voxel::Barricade);

    REQUIRE(box_overlaps_solid(world, position, kPlayer));
    const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kInstantEject);

    CHECK(d.penetrating);
    CHECK(d.resolved);
    CHECK_FALSE(box_overlaps_solid(world, d.position, kPlayer));
    // Cleared past y = 3, not merely past y = 1.
    CHECK(static_cast<f64>(d.position.y - kPlayer.y) == doctest::Approx(4.0).epsilon(0.001));
}

TEST_CASE("clipping the hull ejects you back into the station, not out of it") {
    // Outside the sector reads Empty, so "the destination is clear" is
    // trivially true out in vacuum. Shortest-push is what keeps a body clipped
    // into the hull from being deposited outside the station.
    VoxelWorld world = deck_world();
    fill_box(world, {0, 1, 0}, {0, 8, 31}, Voxel::HullPlate);  // hull at x = 0

    const vec3 position{1.0f, kRestY, 8.0f};  // lo.x = 0.5, clipped into the hull
    REQUIRE(box_overlaps_solid(world, position, kPlayer));

    const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kInstantEject);
    CHECK(d.resolved);
    CHECK(d.position.x > position.x);  // inboard
    CHECK(d.position.x - kPlayer.x >= 1.0f);
    CHECK_FALSE(move_and_slide(world, d.position, kPlayer, vec3{0.0f}, vec3{0.0f}).out_of_bounds);
}

TEST_CASE("a non-positive eject budget reports the state without moving") {
    VoxelWorld world = deck_world();
    fill_box(world, {7, 1, 7}, {9, 1, 9}, Voxel::Barricade);
    const vec3 position{8.0f, kRestY, 8.0f};

    const DepenetrationResult d = resolve_penetration(world, position, kPlayer, 0.0f);
    CHECK(d.penetrating);
    CHECK_FALSE(d.sealed);
    CHECK_FALSE(d.resolved);
    CHECK(d.position.y == position.y);
}

TEST_CASE("a clear body is left exactly where it is") {
    const VoxelWorld world = deck_world();
    const vec3 position{8.0f, kRestY, 8.0f};
    const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kInstantEject);

    CHECK_FALSE(d.penetrating);
    CHECK_FALSE(d.sealed);
    CHECK(d.resolved);
    CHECK(d.position.x == position.x);
    CHECK(d.position.y == position.y);
    CHECK(d.position.z == position.z);
}

// ---------------------------------------------------------------------------
// Axis order
// ---------------------------------------------------------------------------

TEST_CASE("grounding is decided after horizontal motion, not before") {
    // Y last: you walk off the ledge and then fall in the same step. Y first
    // would resolve the drop at the old x, find the deck still under you, and
    // report a contact you no longer have.
    VoxelWorld world(1, 1, 1);
    fill_box(world, {0, 0, 0}, {15, 0, 31}, Voxel::Bulkhead);

    vec3 position{15.0f, kRestY, 8.0f};
    REQUIRE(box_grounded(world, position, kPlayer));

    const SweepResult first =
        move_and_slide(world, position, kPlayer, vec3{0.8f, -0.01f, 0.0f}, vec3{11.0f, -0.6f, 0.0f});
    CHECK(first.grounded);
    position = first.position;

    const SweepResult second =
        move_and_slide(world, position, kPlayer, vec3{0.8f, -0.01f, 0.0f}, vec3{11.0f, -0.6f, 0.0f});
    CHECK_FALSE(second.grounded);
    CHECK_FALSE(second.hit_y);
    CHECK(second.position.y < position.y);
}

// ---------------------------------------------------------------------------
// Sector boundary
// ---------------------------------------------------------------------------

TEST_CASE("the sector boundary is open vacuum, not an invisible wall") {
    // Consistent with VoxelWorld::at() and raycast_voxels(), which both treat
    // outside-the-sector as Empty. Inventing a wall here would contradict both.
    const VoxelWorld world = deck_world();  // 32 voxels on a side
    vec3 position{30.0f, kRestY, 8.0f};
    bool left = false;

    for (int step = 0; step < 12; ++step) {
        const SweepResult r =
            move_and_slide(world, position, kPlayer, vec3{0.6f, 0.0f, 0.0f}, vec3{11.0f, 0.0f, 0.0f});
        REQUIRE_FALSE(r.hit_x);  // nothing stops you at the hull line
        REQUIRE(r.velocity.x == 11.0f);
        position = r.position;
        left = left || r.out_of_bounds;
    }

    CHECK(left);
    CHECK(static_cast<f64>(position.x) == doctest::Approx(37.2));
    // Past the last deck voxel there is no floor, because there is nothing.
    CHECK_FALSE(box_grounded(world, position, kPlayer));
}

TEST_CASE("falling out through a breached deck is reported and stays finite") {
    // The Breacher takes the floor out; the body leaves the sector downward.
    // Physics does not decide the policy, but it does refuse to let the
    // coordinate run away and grind f32 precision over a long run.
    const VoxelWorld world(1, 1, 1);  // no deck at all
    vec3 position{8.0f, 8.0f, 8.0f};
    vec3 velocity{0.0f, -50.0f, 0.0f};
    bool reported = false;

    for (int step = 0; step < 1000; ++step) {
        const SweepResult r = move_and_slide(world, position, kPlayer, vec3{0.0f, -0.9f, 0.0f}, velocity);
        REQUIRE_FALSE(r.grounded);
        REQUIRE(std::isfinite(r.position.y));
        if (r.position.y < 0.0f) reported = reported || r.out_of_bounds;
        position = r.position;
        velocity = r.velocity;
    }

    CHECK(reported);
    CHECK(position.y >= -kGuardMargin);
    CHECK(static_cast<f64>(position.y) == doctest::Approx(static_cast<f64>(-kGuardMargin)));
    CHECK(velocity.y == 0.0f);  // pinned, so the caller stops integrating into a wall of nothing
}

TEST_CASE("out_of_bounds is not set while inside the sector") {
    const VoxelWorld world = deck_world();
    const SweepResult r = move_and_slide(
        world, vec3{16.0f, kRestY, 16.0f}, kPlayer, vec3{kWalkStep, 0.0f, 0.0f}, vec3{11.0f, 0.0f, 0.0f});
    CHECK_FALSE(r.out_of_bounds);
}

// ---------------------------------------------------------------------------
// Tunnelling
// ---------------------------------------------------------------------------

TEST_CASE("a one-voxel-thick floor stops a body moving at the invariant limit") {
    VoxelWorld world(1, 1, 1);
    fill_box(world, {0, 20, 0}, {31, 20, 31}, Voxel::HullPlate);

    vec3 position{8.0f, 21.0f + kPlayer.y + 0.5f, 8.0f};
    for (int step = 0; step < 40; ++step) {
        const SweepResult r =
            move_and_slide(world, position, kPlayer, vec3{0.0f, -0.999f, 0.0f}, vec3{0.0f, -59.0f, 0.0f});
        position = r.position;
        REQUIRE(position.y - kPlayer.y >= 21.0f);
    }
    CHECK(static_cast<f64>(position.y - kPlayer.y) == doctest::Approx(21.0).epsilon(0.001));
}

#ifdef NDEBUG
TEST_CASE("displacement past the invariant degrades to a swept test, not a tunnel") {
    // Debug builds assert instead; this pins the release-build behaviour, which
    // is conservative rather than merely undefined.
    VoxelWorld world(1, 1, 1);
    fill_box(world, {0, 20, 0}, {31, 20, 31}, Voxel::HullPlate);

    const vec3 position{8.0f, 30.0f, 8.0f};
    const SweepResult r =
        move_and_slide(world, position, kPlayer, vec3{0.0f, -25.0f, 0.0f}, vec3{0.0f, -1500.0f, 0.0f});

    CHECK(r.hit_y);
    CHECK(static_cast<f64>(r.position.y - kPlayer.y) == doctest::Approx(21.0).epsilon(0.001));
}
#endif

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

TEST_CASE("box_grounded distinguishes resting from hovering") {
    const VoxelWorld world = deck_world();
    CHECK(box_grounded(world, vec3{8.0f, kRestY, 8.0f}, kPlayer));
    CHECK_FALSE(box_grounded(world, vec3{8.0f, kRestY + 0.05f, 8.0f}, kPlayer));
    CHECK_FALSE(box_grounded(world, vec3{8.0f, 16.0f, 8.0f}, kPlayer));
}

TEST_CASE("box_overlaps_solid answers the build-mode question") {
    // Sim::place_voxel should refuse a target that intersects a body.
    VoxelWorld world = deck_world();
    const vec3 position{8.0f, kRestY, 8.0f};
    CHECK_FALSE(box_overlaps_solid(world, position, kPlayer));

    world.set(8, 2, 8, Voxel::Barricade);
    CHECK(box_overlaps_solid(world, position, kPlayer));
}

// ---------------------------------------------------------------------------
// Degenerate input
// ---------------------------------------------------------------------------

TEST_CASE("a non-finite position is refused rather than laundered") {
    const VoxelWorld world = deck_world();
    const f32 nan = std::nanf("");
    const SweepResult r =
        move_and_slide(world, vec3{nan, 4.0f, 8.0f}, kPlayer, vec3{0.1f, 0.0f, 0.0f}, vec3{6.0f, 0.0f, 0.0f});

    CHECK(std::isnan(r.position.x));  // not silently teleported to the origin
    CHECK_FALSE(r.hit_x);
    CHECK_FALSE(r.grounded);
    CHECK_FALSE(r.out_of_bounds);

    const DepenetrationResult d = resolve_penetration(world, vec3{nan, 4.0f, 8.0f}, kPlayer, kInstantEject);
    CHECK_FALSE(d.penetrating);
    CHECK_FALSE(d.sealed);
}

TEST_CASE("a non-finite velocity or displacement is recovered, not propagated") {
    const VoxelWorld world = deck_world();
    const vec3 position{8.0f, kRestY, 8.0f};
    const f32 inf = std::numeric_limits<f32>::infinity();

    const SweepResult bad_velocity =
        move_and_slide(world, position, kPlayer, vec3{0.1f, 0.0f, 0.0f}, vec3{inf, 0.0f, 0.0f});
    CHECK(bad_velocity.position.x == position.x);
    CHECK(bad_velocity.velocity.x == 0.0f);

    const SweepResult bad_displacement =
        move_and_slide(world, position, kPlayer, vec3{std::nanf(""), 0.0f, 0.0f}, vec3{6.0f, 0.0f, 0.0f});
    CHECK(bad_displacement.position.x == position.x);
    CHECK(bad_displacement.velocity.x == 0.0f);
}

TEST_CASE("a degenerate box is refused") {
    const VoxelWorld world = deck_world();
    const vec3 position{8.0f, kRestY, 8.0f};
    const vec3 displacement{0.5f, 0.0f, 0.0f};

    const vec3 bad[] = {
        vec3{0.0f, 1.8f, 0.5f},
        vec3{-0.5f, 1.8f, 0.5f},
        vec3{0.5f, std::nanf(""), 0.5f},
        vec3{0.5f, 1.8f, kMaxHalfExtent + 1.0f},
    };

    for (const vec3& half : bad) {
        const SweepResult r = move_and_slide(world, position, half, displacement, vec3{11.0f, 0.0f, 0.0f});
        CHECK(r.position.x == position.x);
        CHECK(r.velocity.x == 11.0f);
        CHECK_FALSE(r.grounded);
        CHECK_FALSE(box_overlaps_solid(world, position, half));
        CHECK_FALSE(box_grounded(world, position, half));
        CHECK_FALSE(resolve_penetration(world, position, half, kInstantEject).penetrating);
    }
}

TEST_CASE("zero displacement resolves nothing but still reports grounding") {
    const VoxelWorld world = deck_world();
    const vec3 position{8.0f, kRestY, 8.0f};
    const SweepResult r = move_and_slide(world, position, kPlayer, vec3{0.0f}, vec3{0.0f});

    CHECK(r.position.x == position.x);
    CHECK(r.position.y == position.y);
    CHECK(r.position.z == position.z);
    CHECK_FALSE(r.hit_x);
    CHECK_FALSE(r.hit_y);
    CHECK_FALSE(r.hit_z);
    CHECK(r.grounded);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("a long run is bit-identical when replayed") {
    // Replays and daily runs depend on this. Anything order-dependent in the
    // resolution shows up here as a drifting last bit.
    VoxelWorld world = deck_world();
    fill_box(world, {10, 1, 0}, {10, 8, 31}, Voxel::HullPlate);
    fill_box(world, {0, 1, 12}, {31, 8, 12}, Voxel::HullPlate);
    fill_box(world, {16, 1, 16}, {17, 2, 17}, Voxel::Barricade);

    // Integrated the way Sim has to: gravity clamped to a terminal velocity,
    // which is also what keeps per-axis displacement inside the one-voxel
    // invariant. 55 voxels/s is 0.917 voxels per step at 60 Hz.
    const auto run = [&world](vec3& position, vec3& velocity) {
        for (int step = 0; step < 600; ++step) {
            const DepenetrationResult d = resolve_penetration(world, position, kPlayer, kEjectStep);
            position = d.position;

            const f32 fallen = velocity.y - 40.0f * kDt;
            velocity.y = fallen < -55.0f ? -55.0f : fallen;
            const f32 phase = static_cast<f32>(step % 120) * 0.05f;
            velocity.x = 11.0f * std::cos(phase);
            velocity.z = 11.0f * std::sin(phase);

            const SweepResult r = move_and_slide(world, position, kPlayer, velocity * kDt, velocity);
            position = r.position;
            velocity = r.velocity;
        }
    };

    vec3 pos_a{16.5f, 2.0f, 16.5f};
    vec3 vel_a{0.0f};
    run(pos_a, vel_a);

    vec3 pos_b{16.5f, 2.0f, 16.5f};
    vec3 vel_b{0.0f};
    run(pos_b, vel_b);

    CHECK(pos_a.x == pos_b.x);
    CHECK(pos_a.y == pos_b.y);
    CHECK(pos_a.z == pos_b.z);
    CHECK(vel_a.x == vel_b.x);
    CHECK(vel_a.y == vel_b.y);
    CHECK(vel_a.z == vel_b.z);
    CHECK(std::isfinite(pos_a.x));
    CHECK(std::isfinite(pos_a.y));
    CHECK(std::isfinite(pos_a.z));
    CHECK_FALSE(box_overlaps_solid(world, pos_a, kPlayer));
}
