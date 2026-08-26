// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "voxel/chunk.hpp"
#include "voxel/voxel_world.hpp"

#include <doctest/doctest.h>

using namespace df;

TEST_CASE("a fresh chunk is uniform and allocates nothing") {
    Chunk chunk(Voxel::Empty);
    CHECK(chunk.is_uniform());
    CHECK(chunk.is_empty());
    CHECK(chunk.heap_bytes() == 0);
}

TEST_CASE("writing the same value keeps a chunk compact") {
    Chunk chunk(Voxel::Bulkhead);
    chunk.set(4, 4, 4, Voxel::Bulkhead);
    CHECK(chunk.is_uniform());
    CHECK(chunk.heap_bytes() == 0);
}

TEST_CASE("writing a differing value materialises dense storage exactly once") {
    Chunk chunk(Voxel::Empty);
    chunk.set(1, 2, 3, Voxel::Ore);
    CHECK_FALSE(chunk.is_uniform());
    CHECK(chunk.at(1, 2, 3) == Voxel::Ore);
    CHECK(chunk.at(1, 2, 4) == Voxel::Empty);

    const usize bytes = chunk.heap_bytes();
    chunk.set(5, 5, 5, Voxel::Ice);
    CHECK(chunk.heap_bytes() == bytes);  // no second allocation
}

TEST_CASE("out of bounds access is safe") {
    Chunk chunk(Voxel::Bulkhead);
    CHECK(chunk.at(-1, 0, 0) == Voxel::Empty);
    CHECK(chunk.at(Chunk::kSize, 0, 0) == Voxel::Empty);
    chunk.set(-1, 0, 0, Voxel::Ore);  // must not crash or corrupt
    CHECK(chunk.is_uniform());
}

TEST_CASE("damage accumulates and destroys at toughness") {
    Chunk chunk(Voxel::Empty);
    chunk.set(0, 0, 0, Voxel::HullPlate);
    const u8 toughness = voxel_toughness(Voxel::HullPlate);
    REQUIRE(toughness == 3);

    CHECK_FALSE(chunk.damage(0, 0, 0, 1));
    CHECK(chunk.damage_at(0, 0, 0) == 1);
    CHECK_FALSE(chunk.damage(0, 0, 0, 1));
    CHECK(chunk.damage(0, 0, 0, 1));  // third hit destroys
    CHECK(chunk.at(0, 0, 0) == Voxel::Empty);
    CHECK(chunk.damage_at(0, 0, 0) == 0);  // damage cleared with the voxel
}

TEST_CASE("damaging empty space does nothing") {
    Chunk chunk(Voxel::Empty);
    CHECK_FALSE(chunk.damage(0, 0, 0, 255));
}

TEST_CASE("reinforced barricades outlast plain ones") {
    CHECK(voxel_toughness(Voxel::Reinforced) > voxel_toughness(Voxel::Barricade));
}

TEST_CASE("world maps coordinates across chunk boundaries") {
    VoxelWorld world(2, 1, 1);
    REQUIRE(world.size_in_voxels().x == 64);

    world.set(33, 0, 0, Voxel::Ore);
    CHECK(world.at(33, 0, 0) == Voxel::Ore);
    CHECK(VoxelWorld::to_chunk_coord({33, 0, 0}).x == 1);
    CHECK(VoxelWorld::to_local({33, 0, 0}).x == 1);
}

TEST_CASE("world reads outside the sector as empty") {
    VoxelWorld world(1, 1, 1);
    CHECK(world.at(-1, 0, 0) == Voxel::Empty);
    CHECK(world.at(999, 0, 0) == Voxel::Empty);
    CHECK_FALSE(world.contains({-1, 0, 0}));
    world.set(-1, 0, 0, Voxel::Ore);  // must be ignored, not wrap around
    CHECK(world.at(31, 0, 0) == Voxel::Empty);
}

TEST_CASE("editing a chunk face dirties the neighbouring chunk too") {
    VoxelWorld world(2, 1, 1);
    (void)world.take_dirty_chunks();  // clear the initial all-dirty state

    world.set(31, 0, 0, Voxel::Bulkhead);  // last column of chunk 0

    const auto dirty = world.take_dirty_chunks();
    bool saw_owner = false;
    bool saw_neighbour = false;
    for (const auto& coord : dirty) {
        if (coord.x == 0) saw_owner = true;
        if (coord.x == 1) saw_neighbour = true;
    }
    CHECK(saw_owner);
    CHECK(saw_neighbour);
}

TEST_CASE("an interior edit dirties only its own chunk") {
    VoxelWorld world(2, 1, 1);
    (void)world.take_dirty_chunks();

    world.set(15, 15, 15, Voxel::Bulkhead);

    const auto dirty = world.take_dirty_chunks();
    CHECK(dirty.size() == 1);
    CHECK(dirty[0].x == 0);
}

TEST_CASE("sector generation is deterministic in its seed") {
    VoxelWorld a(3, 2, 3);
    VoxelWorld b(3, 2, 3);
    generate_test_sector(a, 42);
    generate_test_sector(b, 42);

    const ivec3 extent = a.size_in_voxels();
    for (i32 z = 0; z < extent.z; z += 7) {
        for (i32 y = 0; y < extent.y; y += 3) {
            for (i32 x = 0; x < extent.x; x += 7) {
                REQUIRE(a.at(x, y, z) == b.at(x, y, z));
            }
        }
    }
}

TEST_CASE("a generated sector fits a phone memory budget") {
    VoxelWorld world(7, 2, 7);  // 224 x 64 x 224 voxels, the shipping sector size
    generate_test_sector(world, 7);

    const f64 megabytes = static_cast<f64>(world.heap_bytes()) / (1024.0 * 1024.0);
    INFO("sector heap = " << megabytes << " MB");
    // The whole app budget is ~700 MB. Voxel storage gets a small slice of it;
    // if this assert ever fires, the sector grew or the compaction broke.
    CHECK(megabytes < 64.0);
}
