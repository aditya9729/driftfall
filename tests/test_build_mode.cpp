// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "game/build_mode.hpp"
#include "game/sim.hpp"  // build_cost

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace df;

namespace {

constexpr f32 kNaN = std::numeric_limits<f32>::quiet_NaN();
constexpr f32 kInf = std::numeric_limits<f32>::infinity();

/// One chunk of vacuum with a single solid voxel in it. Small on purpose: the
/// sector boundary is one of the things under test, so it needs to be close.
VoxelWorld world_with(ivec3 solid) {
    VoxelWorld world(1, 1, 1);
    world.set(solid, Voxel::HullPlate);
    return world;
}

/// Deck plating across the whole y=0 layer, as generate_test_sector lays it.
VoxelWorld world_with_deck() {
    VoxelWorld world(1, 1, 1);
    const ivec3 extent = world.size_in_voxels();
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 x = 0; x < extent.x; ++x) world.set(x, 0, z, Voxel::Bulkhead);
    }
    return world;
}

BuildCursor cursor_at(const VoxelWorld& world, vec3 origin, vec3 direction, i32 salvage = 1000) {
    return compute_build_cursor(world, origin, direction, kBuildReach, Voxel::Barricade, salvage);
}

}  // namespace

// ---------------------------------------------------------------------------
// Placement geometry
// ---------------------------------------------------------------------------

TEST_CASE("a block lands against the face the ray came in through, on all six sides") {
    // The one rule the player has to be able to predict before spending
    // salvage: the block goes on the side you are pointing at.
    const ivec3 wall{16, 16, 16};
    const VoxelWorld world = world_with(wall);

    struct Case {
        vec3 from;
        vec3 dir;
        ivec3 expected_target;
    };

    const Case cases[] = {
        {{10.5f, 16.5f, 16.5f}, {1.0f, 0.0f, 0.0f}, {15, 16, 16}},
        {{22.5f, 16.5f, 16.5f}, {-1.0f, 0.0f, 0.0f}, {17, 16, 16}},
        {{16.5f, 10.5f, 16.5f}, {0.0f, 1.0f, 0.0f}, {16, 15, 16}},
        {{16.5f, 22.5f, 16.5f}, {0.0f, -1.0f, 0.0f}, {16, 17, 16}},
        {{16.5f, 16.5f, 10.5f}, {0.0f, 0.0f, 1.0f}, {16, 16, 15}},
        {{16.5f, 16.5f, 22.5f}, {0.0f, 0.0f, -1.0f}, {16, 16, 17}},
    };

    for (const Case& c : cases) {
        const BuildCursor cursor = cursor_at(world, c.from, c.dir);
        REQUIRE(cursor.valid);
        CHECK(cursor.target.x == c.expected_target.x);
        CHECK(cursor.target.y == c.expected_target.y);
        CHECK(cursor.target.z == c.expected_target.z);
        // The anchor is always the voxel that was hit, never the new cell.
        CHECK(cursor.anchor.x == wall.x);
        CHECK(cursor.anchor.y == wall.y);
        CHECK(cursor.anchor.z == wall.z);
        CHECK_FALSE(is_solid(world.at(cursor.target)));
    }
}

TEST_CASE("the target cell is always the empty one, never the wall itself") {
    // Two voxels deep: the cursor must stop in front of the first, not tunnel
    // to the far side of the pair.
    VoxelWorld world(1, 1, 1);
    world.set(16, 8, 8, Voxel::HullPlate);
    world.set(17, 8, 8, Voxel::HullPlate);

    const BuildCursor cursor = cursor_at(world, vec3{10.5f, 8.5f, 8.5f}, vec3{1.0f, 0.0f, 0.0f});
    REQUIRE(cursor.valid);
    CHECK(cursor.target.x == 15);
    CHECK(cursor.anchor.x == 16);
}

TEST_CASE("looking at nothing within reach is not a build") {
    const VoxelWorld world = world_with({16, 8, 8});

    // Open vacuum in the other direction.
    CHECK_FALSE(cursor_at(world, vec3{8.5f, 8.5f, 8.5f}, vec3{0.0f, 1.0f, 0.0f}).valid);

    // The wall exists but is 7.5 voxels away, and reach is what decides.
    const vec3 from{8.5f, 8.5f, 8.5f};
    const vec3 dir{1.0f, 0.0f, 0.0f};
    CHECK_FALSE(compute_build_cursor(world, from, dir, 7.0f, Voxel::Barricade, 1000).valid);
    CHECK(compute_build_cursor(world, from, dir, 8.0f, Voxel::Barricade, 1000).valid);
}

TEST_CASE("a target outside the sector is refused rather than clamped") {
    // Approaching the hull from outside: the face you would build on points
    // into vacuum. VoxelWorld::set() silently drops that write, so accepting
    // the cursor would charge the player for a voxel that never appears.
    const VoxelWorld world = world_with({0, 8, 8});

    const BuildCursor cursor = cursor_at(world, vec3{-6.5f, 8.5f, 8.5f}, vec3{1.0f, 0.0f, 0.0f});
    CHECK_FALSE(cursor.valid);

    // Same wall from inside the sector: legal, and the cell is in bounds.
    const BuildCursor inside = cursor_at(world, vec3{6.5f, 8.5f, 8.5f}, vec3{-1.0f, 0.0f, 0.0f});
    REQUIRE(inside.valid);
    CHECK(inside.target.x == 1);
    CHECK(world.contains(inside.target));
}

TEST_CASE("a ray that starts buried in a wall names no face to build on") {
    // raycast_voxels reports a hit with a zero normal here. There is no side to
    // attach to, and picking one would put the block where nobody pointed.
    const VoxelWorld world = world_with({8, 8, 8});
    const BuildCursor cursor = cursor_at(world, vec3{8.5f, 8.5f, 8.5f}, vec3{1.0f, 0.0f, 0.0f});
    CHECK_FALSE(cursor.valid);
}

// ---------------------------------------------------------------------------
// Economy
// ---------------------------------------------------------------------------

TEST_CASE("affordability flips exactly at the price, and never gates validity") {
    const VoxelWorld world = world_with({16, 8, 8});
    const vec3 from{10.5f, 8.5f, 8.5f};
    const vec3 dir{1.0f, 0.0f, 0.0f};
    const i32 cost = build_cost(Voxel::Reinforced);
    REQUIRE(cost == 9);

    const BuildCursor broke =
        compute_build_cursor(world, from, dir, kBuildReach, Voxel::Reinforced, cost - 1);
    const BuildCursor exact = compute_build_cursor(world, from, dir, kBuildReach, Voxel::Reinforced, cost);

    CHECK(broke.cost == cost);
    CHECK(exact.cost == cost);
    // Both cursors are legal placements. Only one can be paid for — the HUD
    // draws the other greyed out, because "aiming at nothing" and "broke" are
    // different problems with different fixes.
    CHECK(broke.valid);
    CHECK(exact.valid);
    CHECK_FALSE(broke.affordable);
    CHECK(exact.affordable);
}

TEST_CASE("a material that is not buildable has no cursor at any price") {
    const VoxelWorld world = world_with({16, 8, 8});
    const vec3 from{10.5f, 8.5f, 8.5f};
    const vec3 dir{1.0f, 0.0f, 0.0f};

    for (const Voxel material : {Voxel::Empty, Voxel::HullPlate, Voxel::Bulkhead, Voxel::Ore, Voxel::Ice}) {
        const BuildCursor cursor = compute_build_cursor(world, from, dir, kBuildReach, material, 999999);
        CHECK_FALSE(cursor.valid);
        CHECK_FALSE(cursor.affordable);
        CHECK(cursor.cost == 0);
    }
}

// ---------------------------------------------------------------------------
// Degenerate input
// ---------------------------------------------------------------------------

TEST_CASE("a degenerate ray is refused rather than resolved") {
    const VoxelWorld world = world_with({16, 8, 8});
    const vec3 from{10.5f, 8.5f, 8.5f};
    const vec3 dir{1.0f, 0.0f, 0.0f};

    CHECK_FALSE(cursor_at(world, from, vec3{0.0f, 0.0f, 0.0f}).valid);
    CHECK_FALSE(cursor_at(world, from, vec3{kNaN, 0.0f, 0.0f}).valid);
    CHECK_FALSE(cursor_at(world, from, vec3{0.0f, kInf, 0.0f}).valid);
    CHECK_FALSE(cursor_at(world, from, vec3{1.0f, 0.0f, -kInf}).valid);

    // A NaN origin would be floored straight into an i32 by raycast_voxels,
    // which does not check it. This layer has to.
    CHECK_FALSE(cursor_at(world, vec3{kNaN, 8.5f, 8.5f}, dir).valid);
    CHECK_FALSE(cursor_at(world, vec3{kInf, kInf, kInf}, dir).valid);

    CHECK_FALSE(compute_build_cursor(world, from, dir, 0.0f, Voxel::Barricade, 1000).valid);
    CHECK_FALSE(compute_build_cursor(world, from, dir, -12.0f, Voxel::Barricade, 1000).valid);
    CHECK_FALSE(compute_build_cursor(world, from, dir, kNaN, Voxel::Barricade, 1000).valid);
}

TEST_CASE("direction need not be normalised") {
    const VoxelWorld world = world_with({16, 8, 8});
    const vec3 from{10.5f, 8.5f, 8.5f};
    const BuildCursor unit = cursor_at(world, from, vec3{1.0f, 0.0f, 0.0f});
    const BuildCursor scaled = cursor_at(world, from, vec3{41.0f, 0.0f, 0.0f});

    REQUIRE(unit.valid);
    REQUIRE(scaled.valid);
    CHECK(unit.target.x == scaled.target.x);
}

// ---------------------------------------------------------------------------
// Body overlap
// ---------------------------------------------------------------------------

TEST_CASE("a body flush against a cell is not inside it") {
    // The boundary case the whole predicate turns on. A player resting on the
    // deck has their feet exactly on a voxel plane; if flush counted as
    // overlap, standing still would forbid building the cell you stand on and
    // physics.hpp and this file would disagree about what touching means.
    const vec3 half = kPlayerHalfExtents;
    const vec3 centre{10.5f, 2.8f, 10.5f};  // x,z span [10,11]; y span [1.0, 4.6]

    CHECK(cell_intersects_body({10, 2, 10}, centre, half));  // squarely inside

    // Flush on -x and +x: spans meet exactly at 10.0 and 11.0.
    CHECK_FALSE(cell_intersects_body({9, 2, 10}, centre, half));
    CHECK_FALSE(cell_intersects_body({11, 2, 10}, centre, half));
    CHECK_FALSE(cell_intersects_body({10, 2, 9}, centre, half));
    CHECK_FALSE(cell_intersects_body({10, 2, 11}, centre, half));

    // Flush underfoot: the body's lowest point is exactly y = 1.0, and cell
    // y=0 is the half-open box [0, 1). This is the deck you are standing on.
    CHECK_FALSE(cell_intersects_body({10, 0, 10}, centre, half));
    // One voxel further into the body on each end.
    CHECK(cell_intersects_body({10, 1, 10}, centre, half));
    CHECK(cell_intersects_body({10, 4, 10}, centre, half));  // head, top at 4.6
    CHECK_FALSE(cell_intersects_body({10, 5, 10}, centre, half));
}

TEST_CASE("building into your own feet is caught before it happens") {
    // The architecture-review case: a barricade in the cell you occupy seals
    // you inside geometry, and de-penetration is a much more expensive cure
    // than this comparison is a prevention.
    const vec3 player{16.5f, 2.8f, 16.5f};

    CHECK(cell_intersects_body({16, 1, 16}, player, kPlayerHalfExtents));  // feet
    CHECK(cell_intersects_body({16, 3, 16}, player, kPlayerHalfExtents));  // chest
    // One cell forward is clear: you can still wall yourself in deliberately,
    // which is the point of fortifying.
    CHECK_FALSE(cell_intersects_body({17, 3, 16}, player, kPlayerHalfExtents));
}

TEST_CASE("a body we cannot describe blocks the build rather than licensing it") {
    const vec3 half = kPlayerHalfExtents;
    CHECK(cell_intersects_body({0, 0, 0}, vec3{kNaN, 0.0f, 0.0f}, half));
    CHECK(cell_intersects_body({0, 0, 0}, vec3{0.0f, kInf, 0.0f}, half));
    CHECK(cell_intersects_body({0, 0, 0}, vec3{0.5f, 0.5f, 0.5f}, vec3{kNaN, 1.0f, 1.0f}));
    CHECK(cell_intersects_body({0, 0, 0}, vec3{0.5f, 0.5f, 0.5f}, vec3{-1.0f, 1.0f, 1.0f}));
}

TEST_CASE("a zero-extent body is a point, and a point on a face is outside") {
    const vec3 none{0.0f, 0.0f, 0.0f};
    CHECK(cell_intersects_body({4, 4, 4}, vec3{4.5f, 4.5f, 4.5f}, none));
    CHECK_FALSE(cell_intersects_body({4, 4, 4}, vec3{4.0f, 4.5f, 4.5f}, none));
    CHECK_FALSE(cell_intersects_body({4, 4, 4}, vec3{5.0f, 4.5f, 4.5f}, none));
}

// ---------------------------------------------------------------------------
// Turret siting
// ---------------------------------------------------------------------------

TEST_CASE("a turret site needs a clear column and a floor under it") {
    VoxelWorld world = world_with_deck();

    CHECK(turret_site_clear(world, {8, 1, 8}));

    // No floor: the deck is two voxels below, not one.
    CHECK_FALSE(turret_site_clear(world, {8, 2, 8}));

    // Floor but no headroom. The turret is kTurretHeightVoxels tall, so a
    // ceiling directly above its second cell refuses the site.
    world.set(8, 2, 8, Voxel::HullPlate);
    CHECK_FALSE(turret_site_clear(world, {8, 1, 8}));

    // The base cell itself being occupied is refused too — otherwise you could
    // deploy into the voxel you are about to seal.
    VoxelWorld blocked = world_with_deck();
    blocked.set(9, 1, 9, Voxel::Barricade);
    CHECK_FALSE(turret_site_clear(blocked, {9, 1, 9}));
}

TEST_CASE("a turret site is refused when its column leaves the sector") {
    VoxelWorld world(1, 1, 1);
    const ivec3 extent = world.size_in_voxels();
    world.set(8, extent.y - 1, 8, Voxel::HullPlate);
    // Standing on the topmost voxel puts the whole column outside the sector.
    CHECK_FALSE(turret_site_clear(world, {8, extent.y, 8}));
}

TEST_CASE("looking at the deck resolves a turret site; looking at a wall does not") {
    VoxelWorld world = world_with_deck();
    const i32 cost = 60;

    const TurretSite floor_site =
        compute_turret_site(world, vec3{8.5f, 6.0f, 8.5f}, vec3{0.0f, -1.0f, 0.0f}, kBuildReach, cost, 120);
    REQUIRE(floor_site.valid);
    CHECK(floor_site.base.x == 8);
    CHECK(floor_site.base.y == 1);
    CHECK(floor_site.base.z == 8);
    CHECK(floor_site.anchor.y == 0);
    // Centre of the 1 x 2 x 1 column, which is the convention the physics box
    // extents are measured from.
    CHECK(static_cast<f64>(floor_site.position.x) == doctest::Approx(8.5));
    CHECK(static_cast<f64>(floor_site.position.y) == doctest::Approx(2.0));
    CHECK(static_cast<f64>(floor_site.position.z) == doctest::Approx(8.5));
    CHECK(floor_site.affordable);

    // A wall hit gives an inward-facing normal, not an up-facing one. Refused
    // rather than slid to some nearby patch of deck: a placement that moves
    // after you commit is a placement you cannot aim.
    for (i32 y = 1; y <= 6; ++y) world.set(14, y, 8, Voxel::HullPlate);
    const TurretSite wall_site =
        compute_turret_site(world, vec3{8.5f, 3.5f, 8.5f}, vec3{1.0f, 0.0f, 0.0f}, kBuildReach, cost, 120);
    CHECK_FALSE(wall_site.valid);
}

TEST_CASE("a turret site reports affordability at exactly the price") {
    const VoxelWorld world = world_with_deck();
    const vec3 from{8.5f, 6.0f, 8.5f};
    const vec3 down{0.0f, -1.0f, 0.0f};

    const TurretSite broke = compute_turret_site(world, from, down, kBuildReach, 60, 59);
    const TurretSite exact = compute_turret_site(world, from, down, kBuildReach, 60, 60);
    REQUIRE(broke.valid);
    REQUIRE(exact.valid);
    CHECK(broke.cost == 60);
    CHECK_FALSE(broke.affordable);
    CHECK(exact.affordable);
}

TEST_CASE("a degenerate ray yields no turret site") {
    const VoxelWorld world = world_with_deck();
    const vec3 from{8.5f, 6.0f, 8.5f};

    CHECK_FALSE(compute_turret_site(world, from, vec3{0.0f}, kBuildReach, 60, 120).valid);
    CHECK_FALSE(compute_turret_site(world, from, vec3{0.0f, kNaN, 0.0f}, kBuildReach, 60, 120).valid);
    CHECK_FALSE(
        compute_turret_site(world, vec3{kNaN, 6.0f, 8.5f}, vec3{0.0f, -1.0f, 0.0f}, kBuildReach, 60, 120)
            .valid);
    CHECK_FALSE(compute_turret_site(world, from, vec3{0.0f, -1.0f, 0.0f}, 0.0f, 60, 120).valid);
    CHECK_FALSE(compute_turret_site(world, from, vec3{0.0f, -1.0f, 0.0f}, -1.0f, 60, 120).valid);
}

// ---------------------------------------------------------------------------
// Scale
// ---------------------------------------------------------------------------

TEST_CASE("the locked scale holds: 1 voxel is half a metre and the player is 1.8 m") {
    // These feed the reach justification and the body box. If someone retunes
    // the voxel size, this is where it should hurt first.
    CHECK(static_cast<f64>(kVoxelMetres) == doctest::Approx(0.5));
    CHECK(static_cast<f64>(kPlayerHalfExtents.y * 2.0f * kVoxelMetres) == doctest::Approx(1.8));
    CHECK(static_cast<f64>(kPlayerHalfExtents.x * 2.0f * kVoxelMetres) == doctest::Approx(0.5));
    // 7 voxels of reach is 3.5 m from Camera::head(), well inside the
    // 24-voxel room grid — you cannot seal a doorway you are not standing at.
    CHECK(static_cast<f64>(kBuildReach * kVoxelMetres) == doctest::Approx(3.5));
    CHECK(kBuildReach * 2.0f < 24.0f);
}
