// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "voxel/greedy_mesher.hpp"

#include <doctest/doctest.h>

#include <array>

using namespace df;

namespace {

/// Every quad is 4 vertices and 6 indices. If this ever stops holding, the
/// mesher started sharing vertices and the packing assumptions changed.
void check_mesh_invariants(const ChunkMesh& mesh) {
    CHECK(mesh.vertices.size() == mesh.quads * 4u);
    CHECK(mesh.indices.size() == mesh.quads * 6u);
    for (const u32 index : mesh.indices) {
        REQUIRE(index < mesh.vertices.size());
    }
    for (const PackedVertex& vertex : mesh.vertices) {
        // Chunk-local positions live in [0, kSize] inclusive: a face on the
        // far wall sits at 32.
        CHECK(vertex.x <= Chunk::kSize);
        CHECK(vertex.y <= Chunk::kSize);
        CHECK(vertex.z <= Chunk::kSize);
        CHECK(vertex.normal_index() < 6);
        CHECK(vertex.material != static_cast<u8>(Voxel::Empty));
    }
}

}  // namespace

TEST_CASE("an empty chunk produces no geometry") {
    VoxelWorld world(1, 1, 1);
    const ChunkMesh mesh = greedy_mesh(world, {0, 0, 0});
    CHECK(mesh.empty());
    CHECK(mesh.quads == 0);
}

TEST_CASE("a lone voxel produces exactly six quads") {
    VoxelWorld world(1, 1, 1);
    world.set(10, 10, 10, Voxel::HullPlate);

    const ChunkMesh mesh = greedy_mesh(world, {0, 0, 0});
    CHECK(mesh.quads == 6);
    CHECK(mesh.faces == 6);
    CHECK(mesh.vertices.size() == 24);
    CHECK(mesh.indices.size() == 36);
    check_mesh_invariants(mesh);

    // All six axis directions must be represented exactly once.
    std::array<int, 6> seen{};
    for (const PackedVertex& v : mesh.vertices) ++seen[v.normal_index()];
    for (const int count : seen) CHECK(count == 4);
}

TEST_CASE("a flat slab merges into one quad per face") {
    VoxelWorld world(1, 1, 1);
    // A full 32x32 layer, one voxel thick.
    for (i32 z = 0; z < Chunk::kSize; ++z) {
        for (i32 x = 0; x < Chunk::kSize; ++x) {
            world.set(x, 5, z, Voxel::Bulkhead);
        }
    }

    const ChunkMesh mesh = greedy_mesh(world, {0, 0, 0});
    check_mesh_invariants(mesh);

    // Top and bottom merge to one quad each; the four sides are 32x1 strips
    // that also merge to one quad each. Six quads for 1024 voxels.
    CHECK(mesh.quads == 6);
    CHECK(mesh.faces == 32 * 32 * 2 + 32 * 4);
    CHECK(mesh.faces == count_naive_faces(world, {0, 0, 0}));
}

TEST_CASE("differing materials refuse to merge") {
    VoxelWorld world(1, 1, 1);
    for (i32 x = 0; x < Chunk::kSize; ++x) {
        world.set(x, 5, 5, x < 16 ? Voxel::Bulkhead : Voxel::Ore);
    }

    const ChunkMesh mesh = greedy_mesh(world, {0, 0, 0});
    check_mesh_invariants(mesh);

    // The +Y face of the strip cannot be one quad any more: it splits at the
    // material boundary. Same for -Y, +Z, -Z.
    bool saw_bulkhead = false;
    bool saw_ore = false;
    for (const PackedVertex& v : mesh.vertices) {
        saw_bulkhead |= v.material == static_cast<u8>(Voxel::Bulkhead);
        saw_ore |= v.material == static_cast<u8>(Voxel::Ore);
    }
    CHECK(saw_bulkhead);
    CHECK(saw_ore);
}

TEST_CASE("damage state splits quads so cracks render per voxel") {
    VoxelWorld world(1, 1, 1);
    for (i32 x = 0; x < 8; ++x) world.set(x, 5, 5, Voxel::Bulkhead);

    const ChunkMesh before = greedy_mesh(world, {0, 0, 0});
    world.damage({3, 5, 5}, 1);
    const ChunkMesh after = greedy_mesh(world, {0, 0, 0});

    CHECK(after.quads > before.quads);
    CHECK(after.faces == before.faces);  // same surface, just cut into more pieces
    check_mesh_invariants(after);
}

TEST_CASE("a fully enclosed chunk emits nothing") {
    // 3x3x3 chunks, everything solid. The centre chunk sees a solid neighbour
    // in every direction, so not one of its faces is visible.
    VoxelWorld world(3, 3, 3);
    const ivec3 extent = world.size_in_voxels();
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 y = 0; y < extent.y; ++y) {
            for (i32 x = 0; x < extent.x; ++x) {
                world.set(x, y, z, Voxel::Bulkhead);
            }
        }
    }

    const ChunkMesh centre = greedy_mesh(world, {1, 1, 1});
    CHECK(centre.empty());
    CHECK(count_naive_faces(world, {1, 1, 1}) == 0);

    // A corner chunk, by contrast, has three exposed sides — one merged quad
    // each, since the whole face is a single material.
    const ChunkMesh corner = greedy_mesh(world, {0, 0, 0});
    CHECK(corner.quads == 3);
    check_mesh_invariants(corner);
}

TEST_CASE("no faces are emitted across a shared chunk seam") {
    VoxelWorld world(2, 1, 1);
    const ivec3 extent = world.size_in_voxels();
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 y = 0; y < extent.y; ++y) {
            for (i32 x = 0; x < extent.x; ++x) {
                world.set(x, y, z, Voxel::HullPlate);
            }
        }
    }

    const ChunkMesh right = greedy_mesh(world, {1, 0, 0});
    check_mesh_invariants(right);

    // Chunk 1's -X side abuts solid chunk 0. Normal index 1 is -X: it must
    // not appear at all. Getting this wrong is the classic voxel bug where
    // walls flicker at chunk boundaries.
    for (const PackedVertex& v : right.vertices) {
        CHECK(v.normal_index() != 1);
    }
    CHECK(right.quads == 5);  // +X, +Y, -Y, +Z, -Z
}

TEST_CASE("a face at a chunk seam is emitted by exactly one chunk") {
    // Regression: both chunks sharing a seam used to emit the same face, which
    // renders as z-fighting shimmer along every chunk edge.
    VoxelWorld world(2, 1, 1);
    for (i32 z = 0; z < Chunk::kSize; ++z) {
        for (i32 y = 0; y < Chunk::kSize; ++y) {
            for (i32 x = 0; x < Chunk::kSize; ++x) {
                world.set(x, y, z, Voxel::Bulkhead);  // chunk 0 solid, chunk 1 vacuum
            }
        }
    }
    // Put something in chunk 1 so it is not uniform-empty and actually meshes.
    world.set(40, 10, 10, Voxel::Ore);

    const ChunkMesh left = greedy_mesh(world, {0, 0, 0});
    const ChunkMesh right = greedy_mesh(world, {1, 0, 0});
    check_mesh_invariants(left);
    check_mesh_invariants(right);

    // The slab's +X wall belongs to chunk 0's voxels, so chunk 0 draws it...
    int left_seam_vertices = 0;
    for (const PackedVertex& v : left.vertices) {
        if (v.normal_index() == 0 && v.x == Chunk::kSize) ++left_seam_vertices;
    }
    CHECK(left_seam_vertices == 4);  // exactly one merged quad

    // ...and chunk 1 must not draw it a second time at its own local x == 0.
    for (const PackedVertex& v : right.vertices) {
        const bool duplicates_the_seam = v.normal_index() == 0 && v.x == 0;
        CHECK_FALSE(duplicates_the_seam);
    }

    CHECK(left.faces == count_naive_faces(world, {0, 0, 0}));
    CHECK(right.faces == count_naive_faces(world, {1, 0, 0}));
}

TEST_CASE("greedy merging beats naive meshing on a real sector") {
    VoxelWorld world(3, 2, 3);
    generate_test_sector(world, 2024);

    u32 total_quads = 0;
    u32 total_faces = 0;
    const ivec3 chunks = world.size_in_chunks();
    for (i32 z = 0; z < chunks.z; ++z) {
        for (i32 y = 0; y < chunks.y; ++y) {
            for (i32 x = 0; x < chunks.x; ++x) {
                const ChunkMesh mesh = greedy_mesh(world, {x, y, z});
                check_mesh_invariants(mesh);
                total_quads += mesh.quads;
                total_faces += mesh.faces;
                // Every face the mesher accounts for must match the naive count.
                REQUIRE(mesh.faces == count_naive_faces(world, {x, y, z}));
            }
        }
    }

    REQUIRE(total_quads > 0);
    const f64 ratio = static_cast<f64>(total_faces) / static_cast<f64>(total_quads);
    INFO("greedy compression = " << ratio << "x (" << total_faces << " faces -> " << total_quads
                                 << " quads)");
    // If this ever drops below 2x the mesher has regressed into per-face
    // output and the vertex budget is blown.
    CHECK(ratio > 2.0);
}
