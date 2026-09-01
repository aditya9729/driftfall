// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "voxel/raycast.hpp"

#include <doctest/doctest.h>

using namespace df;

namespace {

/// A world with a single solid voxel at the given spot, and nothing else.
VoxelWorld world_with(ivec3 solid) {
    VoxelWorld world(1, 1, 1);
    world.set(solid.x, solid.y, solid.z, Voxel::HullPlate);
    return world;
}

}  // namespace

TEST_CASE("a ray through empty space hits nothing") {
    const VoxelWorld world(1, 1, 1);
    const VoxelHit hit = raycast_voxels(world, vec3{4.5f, 4.5f, 4.5f}, vec3{1.0f, 0.0f, 0.0f}, 64.0f);
    CHECK_FALSE(hit.hit);
    CHECK_FALSE(static_cast<bool>(hit));
}

TEST_CASE("a ray down an axis hits the first solid voxel") {
    const VoxelWorld world = world_with({10, 4, 4});

    const VoxelHit hit = raycast_voxels(world, vec3{4.5f, 4.5f, 4.5f}, vec3{1.0f, 0.0f, 0.0f}, 64.0f);
    REQUIRE(hit.hit);
    CHECK(hit.voxel.x == 10);
    CHECK(hit.voxel.y == 4);
    CHECK(hit.voxel.z == 4);
    // Entered through the -x face, so the outward normal points back at us.
    CHECK(hit.normal.x == -1);
    CHECK(hit.normal.y == 0);
    CHECK(hit.normal.z == 0);
    CHECK(static_cast<f64>(hit.distance) == doctest::Approx(5.5));
}

TEST_CASE("the nearest voxel wins, not merely any voxel on the line") {
    VoxelWorld world(1, 1, 1);
    world.set(8, 4, 4, Voxel::HullPlate);
    world.set(12, 4, 4, Voxel::Bulkhead);

    const VoxelHit hit = raycast_voxels(world, vec3{4.5f, 4.5f, 4.5f}, vec3{1.0f, 0.0f, 0.0f}, 64.0f);
    REQUIRE(hit.hit);
    CHECK(hit.voxel.x == 8);
}

TEST_CASE("a ray finds a voxel from every direction") {
    const VoxelWorld world = world_with({8, 8, 8});
    const vec3 centre{8.5f, 8.5f, 8.5f};

    struct Case {
        vec3 from;
        vec3 dir;
        ivec3 expected_normal;
    };

    const Case cases[] = {
        {{2.5f, 8.5f, 8.5f}, {1.0f, 0.0f, 0.0f}, {-1, 0, 0}},
        {{14.5f, 8.5f, 8.5f}, {-1.0f, 0.0f, 0.0f}, {1, 0, 0}},
        {{8.5f, 2.5f, 8.5f}, {0.0f, 1.0f, 0.0f}, {0, -1, 0}},
        {{8.5f, 14.5f, 8.5f}, {0.0f, -1.0f, 0.0f}, {0, 1, 0}},
        {{8.5f, 8.5f, 2.5f}, {0.0f, 0.0f, 1.0f}, {0, 0, -1}},
        {{8.5f, 8.5f, 14.5f}, {0.0f, 0.0f, -1.0f}, {0, 0, 1}},
    };

    for (const Case& c : cases) {
        const VoxelHit hit = raycast_voxels(world, c.from, c.dir, 64.0f);
        REQUIRE(hit.hit);
        CHECK(hit.voxel.x == 8);
        CHECK(hit.voxel.y == 8);
        CHECK(hit.voxel.z == 8);
        CHECK(hit.normal.x == c.expected_normal.x);
        CHECK(hit.normal.y == c.expected_normal.y);
        CHECK(hit.normal.z == c.expected_normal.z);
    }
    (void)centre;
}

TEST_CASE("max_distance stops the walk short") {
    const VoxelWorld world = world_with({20, 4, 4});
    const vec3 from{4.5f, 4.5f, 4.5f};
    const vec3 dir{1.0f, 0.0f, 0.0f};

    CHECK_FALSE(raycast_voxels(world, from, dir, 8.0f).hit);
    CHECK(raycast_voxels(world, from, dir, 32.0f).hit);
}

TEST_CASE("a ray starting inside a solid voxel hits it immediately") {
    // The muzzle buried in a wall. Reporting a miss here would let the shot
    // pass straight through the thing it is touching.
    const VoxelWorld world = world_with({6, 6, 6});
    const VoxelHit hit = raycast_voxels(world, vec3{6.5f, 6.5f, 6.5f}, vec3{1.0f, 0.0f, 0.0f}, 32.0f);
    REQUIRE(hit.hit);
    CHECK(hit.voxel.x == 6);
    CHECK(static_cast<f64>(hit.distance) == doctest::Approx(0.0));
    // It never crossed a face, so there is no face normal to report.
    CHECK(hit.normal.x == 0);
    CHECK(hit.normal.y == 0);
    CHECK(hit.normal.z == 0);
}

TEST_CASE("a diagonal ray does not tunnel through a wall") {
    // The failure mode of fixed-step sampling: a thin wall slipping between
    // two samples. Walking the grid cannot miss it.
    VoxelWorld world(1, 1, 1);
    for (i32 y = 0; y < 16; ++y) {
        for (i32 z = 0; z < 16; ++z) world.set(10, y, z, Voxel::HullPlate);
    }

    const VoxelHit hit = raycast_voxels(world, vec3{1.5f, 1.5f, 1.5f}, vec3{1.0f, 0.35f, 0.2f}, 64.0f);
    REQUIRE(hit.hit);
    CHECK(hit.voxel.x == 10);
}

TEST_CASE("a degenerate ray is refused rather than looping") {
    const VoxelWorld world = world_with({8, 8, 8});
    CHECK_FALSE(raycast_voxels(world, vec3{4.5f, 4.5f, 4.5f}, vec3{0.0f, 0.0f, 0.0f}, 32.0f).hit);
    CHECK_FALSE(raycast_voxels(world, vec3{4.5f, 4.5f, 4.5f}, vec3{1.0f, 0.0f, 0.0f}, 0.0f).hit);
    CHECK_FALSE(raycast_voxels(world, vec3{4.5f, 4.5f, 4.5f}, vec3{1.0f, 0.0f, 0.0f}, -5.0f).hit);
}

TEST_CASE("a ray aimed out of the sector finds nothing and returns") {
    const VoxelWorld world = world_with({8, 8, 8});
    // Straight up and out through the open ceiling.
    CHECK_FALSE(raycast_voxels(world, vec3{2.5f, 2.5f, 2.5f}, vec3{0.0f, 1.0f, 0.0f}, 500.0f).hit);
}

TEST_CASE("direction need not be normalised") {
    const VoxelWorld world = world_with({10, 4, 4});
    const VoxelHit unit = raycast_voxels(world, vec3{4.5f, 4.5f, 4.5f}, vec3{1.0f, 0.0f, 0.0f}, 64.0f);
    const VoxelHit long_dir = raycast_voxels(world, vec3{4.5f, 4.5f, 4.5f}, vec3{37.0f, 0.0f, 0.0f}, 64.0f);

    REQUIRE(unit.hit);
    REQUIRE(long_dir.hit);
    CHECK(unit.voxel.x == long_dir.voxel.x);
    CHECK(static_cast<f64>(unit.distance) == doctest::Approx(static_cast<f64>(long_dir.distance)));
}
