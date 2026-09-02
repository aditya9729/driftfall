// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "game/nav_field.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <vector>

using namespace df;

namespace {

/// The player body, in voxels. One voxel is 0.5 m, so this is a 0.5 x 1.8 x 0.5
/// voxel half-extent — 1 voxel wide and 4 voxels tall once rounded up, which is
/// the whole reason kCellSize is 4 and doorways have to be 4 voxels tall.
constexpr vec3 kPlayerBody{0.5f, 1.8f, 0.5f};

/// A sector with nothing in it but deck plating. The simplest world in which a
/// body can stand at all, and the baseline for every connectivity test.
VoxelWorld flat_world(i32 chunks_x, i32 chunks_y, i32 chunks_z) {
    VoxelWorld world(chunks_x, chunks_y, chunks_z);
    const ivec3 extent = world.size_in_voxels();
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 x = 0; x < extent.x; ++x) world.set(x, 0, z, Voxel::Bulkhead);
    }
    return world;
}

/// Centre of the cell holding voxel `v`, as a world position. Queries land in
/// the middle of a cell rather than on a boundary, so a test never depends on
/// which side of a face a floor() lands.
vec3 at_voxel(i32 x, i32 y, i32 z) {
    return vec3{static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, static_cast<f32>(z) + 0.5f};
}

f32 length_of(vec3 v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

/// Samples the whole field at cell centres. Used to compare two rebuilds
/// exactly, which is the determinism guarantee the replay system depends on.
std::vector<i32> sample(const NavField& field) {
    const ivec3 cells = field.size_in_cells();
    std::vector<i32> out;
    out.reserve(static_cast<usize>(cells.x * cells.y * cells.z));
    for (i32 z = 0; z < cells.z; ++z) {
        for (i32 y = 0; y < cells.y; ++y) {
            for (i32 x = 0; x < cells.x; ++x) {
                const f32 half = 0.5f * static_cast<f32>(NavField::kCellSize);
                out.push_back(field.distance_at(vec3{static_cast<f32>(x * NavField::kCellSize) + half,
                                                     static_cast<f32>(y * NavField::kCellSize) + half,
                                                     static_cast<f32>(z * NavField::kCellSize) + half}));
            }
        }
    }
    return out;
}

/// Re-punches the generated sector's doorways to a 4-voxel-tall opening — the
/// widening that lands with collision. Everything else about the sector is left
/// exactly as generate_test_sector made it.
void widen_doorways(VoxelWorld& world) {
    const ivec3 extent = world.size_in_voxels();
    constexpr i32 kRoom = 24;
    const auto punch = [&](i32 cx, i32 cz) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            for (i32 dz = -1; dz <= 1; ++dz) {
                for (i32 y = 1; y <= 4; ++y) world.set(cx + dx, y, cz + dz, Voxel::Empty);
            }
        }
    };
    for (i32 z = kRoom; z < extent.z - 1; z += kRoom) {
        for (i32 x = kRoom / 2; x < extent.x - 1; x += kRoom) punch(x, z);
    }
    for (i32 x = kRoom; x < extent.x - 1; x += kRoom) {
        for (i32 z = kRoom / 2; z < extent.z - 1; z += kRoom) punch(x, z);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Sizing
// ---------------------------------------------------------------------------

TEST_CASE("reset sizes the field to the sector, rounding cells up") {
    NavField field;
    field.reset(ivec3{224, 64, 224});
    CHECK(field.size_in_cells().x == 56);
    CHECK(field.size_in_cells().y == 16);
    CHECK(field.size_in_cells().z == 56);
    // Nothing is populated until rebuild runs.
    CHECK(field.reachable_cells() == 0u);
    CHECK(field.distance_at(at_voxel(4, 4, 4)) == -1);
}

TEST_CASE("a sector that does not divide evenly still covers its last voxels") {
    NavField field;
    field.reset(ivec3{9, 5, 1});
    CHECK(field.size_in_cells().x == 3);
    CHECK(field.size_in_cells().y == 2);
    CHECK(field.size_in_cells().z == 1);
}

// ---------------------------------------------------------------------------
// The open case
// ---------------------------------------------------------------------------

TEST_CASE("an open deck is reachable from everywhere on it, and nowhere above it") {
    VoxelWorld world = flat_world(1, 1, 1);  // 32^3, plating at y = 0
    NavField field;
    field.reset(world.size_in_voxels());
    field.rebuild(world, ivec3{16, 1, 16}, kPlayerBody);

    // 8x8 cells of deck, and only those: a body four voxels up has nothing
    // under its feet, so no cell above the floor layer is standable.
    CHECK(field.reachable_cells() == 64u);
    CHECK(field.reachable(at_voxel(1, 1, 1)));
    CHECK(field.reachable(at_voxel(30, 1, 30)));
    CHECK_FALSE(field.reachable(at_voxel(16, 20, 16)));

    // Cell distance, not voxel distance: (0,0,0) to (4,0,4) is eight cell steps.
    CHECK(field.distance_at(at_voxel(1, 1, 1)) == 8);
    CHECK(field.distance_at(at_voxel(16, 1, 16)) == 0);
}

TEST_CASE("direction_at is a unit vector pointing downhill") {
    VoxelWorld world = flat_world(1, 1, 1);
    NavField field;
    field.reset(world.size_in_voxels());
    field.rebuild(world, ivec3{28, 1, 4}, kPlayerBody);

    // Straight down the +x axis from the goal, so there is no tie to break and
    // the expected direction is unambiguous. The field is six-connected: a
    // direction names one axis step plus the pull toward the middle of the
    // corridor, never a diagonal.
    const vec3 from = at_voxel(2, 1, 5);
    const vec3 direction = field.direction_at(from);
    CHECK(static_cast<f64>(length_of(direction)) == doctest::Approx(1.0));
    CHECK(direction.x > 0.0f);
    CHECK(std::fabs(direction.x) > std::fabs(direction.z));

    // Following it lands in a strictly closer cell. That is the property the
    // steering code relies on and the one a broken field silently loses.
    const i32 here = field.distance_at(from);
    const vec3 next = from + direction * static_cast<f32>(NavField::kCellSize);
    CHECK(field.distance_at(next) < here);
    CHECK(field.distance_at(next) >= 0);
}

TEST_CASE("the goal cell itself has no direction to give") {
    VoxelWorld world = flat_world(1, 1, 1);
    NavField field;
    field.reset(world.size_in_voxels());
    field.rebuild(world, ivec3{16, 1, 16}, kPlayerBody);

    // Zero rather than an arbitrary axis: the caller is already there.
    const vec3 direction = field.direction_at(at_voxel(16, 1, 16));
    CHECK(static_cast<f64>(length_of(direction)) == doctest::Approx(0.0));
}

// ---------------------------------------------------------------------------
// Passability: fit, headroom, and something to stand on
// ---------------------------------------------------------------------------

TEST_CASE("a sealed room is reported unreachable rather than given a direction") {
    VoxelWorld world = flat_world(2, 1, 2);  // 64 x 32 x 64
    const ivec3 extent = world.size_in_voxels();

    // A closed box of hull plate around a patch of deck, ceiling included.
    for (i32 y = 1; y <= 8; ++y) {
        for (i32 i = 8; i <= 20; ++i) {
            world.set(i, y, 8, Voxel::HullPlate);
            world.set(i, y, 20, Voxel::HullPlate);
            world.set(8, y, i, Voxel::HullPlate);
            world.set(20, y, i, Voxel::HullPlate);
        }
    }
    for (i32 z = 8; z <= 20; ++z) {
        for (i32 x = 8; x <= 20; ++x) world.set(x, 9, z, Voxel::HullPlate);
    }

    NavField field;
    field.reset(extent);
    field.rebuild(world, ivec3{48, 1, 48}, kPlayerBody);

    const vec3 inside = at_voxel(14, 1, 14);
    CHECK_FALSE(field.reachable(inside));
    CHECK(field.distance_at(inside) == -1);
    // The honest failure: no direction at all, not a plausible-looking one.
    CHECK(static_cast<f64>(length_of(field.direction_at(inside))) == doctest::Approx(0.0));

    // The room being sealed must not have cost us the rest of the deck.
    CHECK(field.reachable(at_voxel(48, 1, 48)));
    CHECK(field.reachable(at_voxel(2, 1, 2)));
}

TEST_CASE("a doorway too narrow for a body does not route that body through it") {
    VoxelWorld world = flat_world(2, 1, 2);  // 64 x 32 x 64
    const ivec3 extent = world.size_in_voxels();

    // A full partition at x = 32 with a single 1-voxel-wide, 4-voxel-tall slot
    // at z = 32. A one-voxel-wide body fits; a three-voxel-wide one does not.
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 y = 1; y <= 6; ++y) world.set(32, y, z, Voxel::HullPlate);
    }
    for (i32 y = 1; y <= 4; ++y) world.set(32, y, 32, Voxel::Empty);

    const ivec3 goal{50, 1, 32};
    const vec3 far_side = at_voxel(10, 1, 32);

    NavField narrow_body;
    narrow_body.reset(extent);
    narrow_body.rebuild(world, goal, kPlayerBody);
    CHECK(narrow_body.reachable(far_side));

    // A Bulwark-sized body: 3 voxels on the ground, still 4 tall. Its footprint
    // cannot occupy the slot, so the field must refuse to route it there.
    NavField wide_body;
    wide_body.reset(extent);
    wide_body.rebuild(world, goal, vec3{1.5f, 1.8f, 1.5f});
    CHECK_FALSE(wide_body.reachable(far_side));
    CHECK(static_cast<f64>(length_of(wide_body.direction_at(far_side))) == doctest::Approx(0.0));
    // It is not that the wide body cannot stand anywhere — only that it cannot
    // get through. Without this the test would pass for the wrong reason.
    CHECK(wide_body.reachable(at_voxel(50, 1, 32)));
}

TEST_CASE("a doorway too short for a body does not route that body through it") {
    const auto build = [](i32 door_height) {
        VoxelWorld world = flat_world(2, 1, 2);
        const ivec3 extent = world.size_in_voxels();
        for (i32 z = 0; z < extent.z; ++z) {
            for (i32 y = 1; y <= 6; ++y) world.set(32, y, z, Voxel::HullPlate);
        }
        for (i32 dz = -1; dz <= 1; ++dz) {
            for (i32 y = 1; y <= door_height; ++y) world.set(32, y, 32 + dz, Voxel::Empty);
        }
        return world;
    };

    const vec3 far_side = at_voxel(10, 1, 32);

    // Three voxels of headroom is 1.5 m. The player body is 1.8 m tall and does
    // not fit — this is the generator's current doorway, and the reason the
    // sector soft-locks.
    const VoxelWorld short_door = build(3);
    NavField over_short;
    over_short.reset(short_door.size_in_voxels());
    over_short.rebuild(short_door, ivec3{50, 1, 32}, kPlayerBody);
    CHECK_FALSE(over_short.reachable(far_side));

    const VoxelWorld tall_door = build(4);
    NavField over_tall;
    over_tall.reset(tall_door.size_in_voxels());
    over_tall.rebuild(tall_door, ivec3{50, 1, 32}, kPlayerBody);
    CHECK(over_tall.reachable(far_side));
}

TEST_CASE("headroom alone is not passability: a body needs something to stand on") {
    VoxelWorld world = flat_world(2, 1, 2);
    const ivec3 extent = world.size_in_voxels();

    // A trench across the whole deck. Wide open overhead, nothing underfoot.
    // A field that only checked that the body fits would walk the horde
    // straight across it.
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 x = 20; x <= 23; ++x) world.set(x, 0, z, Voxel::Empty);
    }

    NavField field;
    field.reset(extent);
    field.rebuild(world, ivec3{50, 1, 32}, kPlayerBody);

    CHECK_FALSE(field.reachable(at_voxel(21, 1, 32)));  // in the trench
    CHECK_FALSE(field.reachable(at_voxel(10, 1, 32)));  // cut off beyond it
    CHECK(field.reachable(at_voxel(30, 1, 32)));        // the goal's side
}

TEST_CASE("a cell straddling a bulkhead does not connect the two sides of it") {
    // The failure a coarse grid invites: a 4-voxel cell containing a 1-voxel
    // wall also contains open floor on both sides, so per-cell passability
    // would mark it passable and let the sweep walk through the wall. The
    // wall here sits at x = 33, one voxel inside the cell that spans 32..35.
    VoxelWorld world = flat_world(2, 1, 2);
    const ivec3 extent = world.size_in_voxels();
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 y = 1; y <= 6; ++y) world.set(33, y, z, Voxel::HullPlate);
    }

    NavField field;
    field.reset(extent);
    field.rebuild(world, ivec3{50, 1, 32}, kPlayerBody);

    CHECK(field.reachable(at_voxel(40, 1, 32)));
    CHECK_FALSE(field.reachable(at_voxel(10, 1, 32)));

    // Nothing on the far side gets a direction either, which is what stops the
    // horde committing to a wall.
    CHECK(static_cast<f64>(length_of(field.direction_at(at_voxel(10, 1, 32)))) == doctest::Approx(0.0));

    // Pinning a known limitation rather than hiding it. The x = 32 strip is the
    // discarded minority group of the cell spanning 32..35, and distance is
    // stored per cell, so a body standing in that one-voxel pocket is still
    // told the cell's distance. It is at most half a cell wide and it cannot
    // widen the reachable set; removing it needs one node per (cell, group),
    // which is a wider distance_ than NavField exposes.
    CHECK(field.reachable(at_voxel(32, 1, 32)));
}

// ---------------------------------------------------------------------------
// The generated sector — the soft-lock regression gate
// ---------------------------------------------------------------------------

TEST_CASE("the generated sector is traversable from the enemy spawn to the player") {
    // THE regression gate. Enemies spawn at the origin (sim.cpp) and the player
    // holds the sector centre, with roughly four full-height bulkheads between
    // them. If this fails, Assault can never clear and the run hangs.
    //
    // It fails today, and it is meant to: generate_test_sector punches 3-voxel
    // doorways and the player body is 4 voxels tall, so every bulkhead is a
    // complete partition. Widening the doorways to 4 voxels is what turns this
    // green — see the companion case below, which does exactly that and passes.
    VoxelWorld world(7, 2, 7);
    generate_test_sector(world, 1337);
    const ivec3 extent = world.size_in_voxels();

    NavField field;
    field.reset(extent);
    field.rebuild(world, ivec3{extent.x / 2, 1, extent.z / 2}, kPlayerBody);

    CHECK(field.reachable(vec3{0.0f, 0.0f, 0.0f}));
}

TEST_CASE("the generated sector is traversable once its doorways are 4 voxels tall") {
    VoxelWorld world(7, 2, 7);
    generate_test_sector(world, 1337);
    widen_doorways(world);
    const ivec3 extent = world.size_in_voxels();

    NavField field;
    field.reset(extent);
    field.rebuild(world, ivec3{extent.x / 2, 1, extent.z / 2}, kPlayerBody);

    CHECK(field.reachable(vec3{0.0f, 0.0f, 0.0f}));
    CHECK(field.distance_at(vec3{0.0f, 0.0f, 0.0f}) > 0);

    // Nearly the whole deck, not merely one room: a sweep confined to the
    // goal's own room would report about 36 cells.
    CHECK(field.reachable_cells() > 3000u);

    // Every room the sector has, reached from the same sweep.
    CHECK(field.reachable(at_voxel(10, 1, 10)));
    CHECK(field.reachable(at_voxel(210, 1, 10)));
    CHECK(field.reachable(at_voxel(10, 1, 210)));
    CHECK(field.reachable(at_voxel(210, 1, 210)));

    // Following the field from the spawn actually arrives, rather than looping
    // or stalling against a bulkhead. One hop per cell of distance is the
    // budget; needing more means the field is not monotonically downhill.
    vec3 position = at_voxel(2, 1, 2);
    i32 remaining = field.distance_at(position);
    REQUIRE(remaining > 0);
    i32 hops = 0;
    const i32 hop_budget = remaining + 8;
    while (remaining > 0 && hops < hop_budget) {
        const vec3 direction = field.direction_at(position);
        REQUIRE(static_cast<f64>(length_of(direction)) == doctest::Approx(1.0));
        position += direction * static_cast<f32>(NavField::kCellSize);
        const i32 next = field.distance_at(position);
        REQUIRE(next >= 0);
        REQUIRE(next < remaining);
        remaining = next;
        ++hops;
    }
    CHECK(remaining == 0);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("two rebuilds of the same world produce identical fields") {
    VoxelWorld world(7, 2, 7);
    generate_test_sector(world, 1337);
    widen_doorways(world);
    const ivec3 extent = world.size_in_voxels();
    const ivec3 goal{extent.x / 2, 1, extent.z / 2};

    NavField field;
    field.reset(extent);
    field.rebuild(world, goal, kPlayerBody);
    const std::vector<i32> first = sample(field);
    const usize first_count = field.reachable_cells();

    field.rebuild(world, goal, kPlayerBody);
    CHECK(sample(field) == first);
    CHECK(field.reachable_cells() == first_count);

    // And a separately generated world with the same seed, since the whole
    // project is deterministic-in-seed for replays.
    VoxelWorld twin(7, 2, 7);
    generate_test_sector(twin, 1337);
    widen_doorways(twin);
    NavField twin_field;
    twin_field.reset(extent);
    twin_field.rebuild(twin, goal, kPlayerBody);
    CHECK(sample(twin_field) == first);
}

TEST_CASE("rebuild leaves no state from the previous sweep behind") {
    VoxelWorld world = flat_world(2, 1, 2);
    const ivec3 extent = world.size_in_voxels();

    NavField field;
    field.reset(extent);
    field.rebuild(world, ivec3{32, 1, 32}, kPlayerBody);
    REQUIRE(field.reachable_cells() > 0u);

    // Rebuilding against an unreachable goal must clear the old field rather
    // than leave last frame's distances lying around.
    field.rebuild(world, ivec3{-100, -100, -100}, kPlayerBody);
    CHECK(field.reachable_cells() == 0u);
    CHECK_FALSE(field.reachable(at_voxel(32, 1, 32)));
    CHECK(field.distance_at(at_voxel(32, 1, 32)) == -1);
}

// ---------------------------------------------------------------------------
// Degenerate input
// ---------------------------------------------------------------------------

TEST_CASE("a zero-size sector produces an empty field instead of crashing") {
    const VoxelWorld world(0, 0, 0);
    NavField field;
    field.reset(world.size_in_voxels());
    CHECK(field.size_in_cells().x == 0);
    field.rebuild(world, ivec3{0, 0, 0}, kPlayerBody);

    CHECK(field.reachable_cells() == 0u);
    CHECK_FALSE(field.reachable(vec3{0.0f}));
    CHECK(field.distance_at(vec3{0.0f}) == -1);
    CHECK(static_cast<f64>(length_of(field.direction_at(vec3{0.0f}))) == doctest::Approx(0.0));
}

TEST_CASE("a goal outside the sector yields no field") {
    VoxelWorld world = flat_world(1, 1, 1);
    NavField field;
    field.reset(world.size_in_voxels());

    field.rebuild(world, ivec3{500, 1, 500}, kPlayerBody);
    CHECK(field.reachable_cells() == 0u);
    CHECK_FALSE(field.reachable(at_voxel(16, 1, 16)));

    field.rebuild(world, ivec3{-8, -8, -8}, kPlayerBody);
    CHECK(field.reachable_cells() == 0u);
}

TEST_CASE("a goal buried in solid rock reaches nothing") {
    VoxelWorld world(1, 1, 1);
    const ivec3 extent = world.size_in_voxels();
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 y = 0; y < extent.y; ++y) {
            for (i32 x = 0; x < extent.x; ++x) world.set(x, y, z, Voxel::Bulkhead);
        }
    }

    NavField field;
    field.reset(extent);
    field.rebuild(world, ivec3{16, 16, 16}, kPlayerBody);

    // The goal cell seeds the sweep whatever is in it — the goal is wherever
    // the player is, and a player mid-jump stands nowhere either. Nothing
    // around it is standable, so the sweep stops there.
    CHECK(field.reachable_cells() == 1u);
    CHECK_FALSE(field.reachable(at_voxel(2, 2, 2)));
    CHECK(field.distance_at(at_voxel(2, 2, 2)) == -1);
}

TEST_CASE("a non-finite query position is refused rather than converted") {
    VoxelWorld world = flat_world(1, 1, 1);
    NavField field;
    field.reset(world.size_in_voxels());
    field.rebuild(world, ivec3{16, 1, 16}, kPlayerBody);

    constexpr f32 nan = std::numeric_limits<f32>::quiet_NaN();
    constexpr f32 inf = std::numeric_limits<f32>::infinity();

    const vec3 bad[] = {
        {nan, 1.5f, 1.5f},
        {1.5f, inf, 1.5f},
        {1.5f, 1.5f, -inf},
        {1.0e30f, 1.5f, 1.5f},
    };
    for (const vec3& p : bad) {
        CHECK_FALSE(field.reachable(p));
        CHECK(field.distance_at(p) == -1);
        CHECK(static_cast<f64>(length_of(field.direction_at(p))) == doctest::Approx(0.0));
    }

    // Below and behind the sector, which is a legal float but not a legal cell.
    CHECK_FALSE(field.reachable(vec3{-4.0f, 1.5f, 1.5f}));
    CHECK(field.distance_at(vec3{1.5f, -4.0f, 1.5f}) == -1);
}

TEST_CASE("a body with a non-finite or negative extent is refused") {
    VoxelWorld world = flat_world(1, 1, 1);
    NavField field;
    field.reset(world.size_in_voxels());

    constexpr f32 nan = std::numeric_limits<f32>::quiet_NaN();
    field.rebuild(world, ivec3{16, 1, 16}, vec3{0.5f, nan, 0.5f});
    CHECK(field.reachable_cells() == 0u);

    field.rebuild(world, ivec3{16, 1, 16}, vec3{-1.0f, 1.8f, 0.5f});
    CHECK(field.reachable_cells() == 0u);

    // A zero extent is a point, not an error: it rounds up to one voxel.
    field.rebuild(world, ivec3{16, 1, 16}, vec3{0.0f, 0.0f, 0.0f});
    CHECK(field.reachable_cells() == 64u);
}

TEST_CASE("rebuild re-sizes itself if the caller skipped reset") {
    VoxelWorld world = flat_world(1, 1, 1);
    NavField field;  // never reset
    field.rebuild(world, ivec3{16, 1, 16}, kPlayerBody);
    CHECK(field.size_in_cells().x == 8);
    CHECK(field.reachable(at_voxel(2, 1, 2)));
}
