// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "voxel/voxel_world.hpp"

#include <vector>

namespace df {

/// One mesh vertex, packed to 8 bytes.
///
/// The naive layout (float3 position + float3 normal + float2 uv) is 32 bytes.
/// At ~40k vertices per visible chunk and dozens of chunks on screen, that
/// difference is the entire vertex-fetch bandwidth budget of a mobile GPU. So:
/// positions are chunk-local integers in [0,32] (a u8 each), the normal is one
/// of six axis directions (3 bits), and ambient occlusion is 4 levels (2 bits).
/// The vertex shader unpacks and offsets by the chunk origin.
struct PackedVertex {
    u8 x = 0, y = 0, z = 0;
    /// bits 7..2 = normal index (0:+X 1:-X 2:+Y 3:-Y 4:+Z 5:-Z), bits 1..0 = AO level
    u8 normal_ao = 0;
    u8 material = 0;
    /// 0 = pristine, 255 = about to break. Drives the crack/glow overlay.
    u8 damage = 0;
    u8 pad0 = 0;
    u8 pad1 = 0;

    [[nodiscard]] u8 normal_index() const { return static_cast<u8>(normal_ao >> 2); }

    [[nodiscard]] u8 ao() const { return static_cast<u8>(normal_ao & 0x3); }
};

static_assert(sizeof(PackedVertex) == 8, "vertex must stay 8 bytes — see comment above");

struct ChunkMesh {
    std::vector<PackedVertex> vertices;
    std::vector<u32> indices;
    /// Number of merged quads emitted. quads * 4 == vertices.size().
    u32 quads = 0;
    /// Number of individual voxel faces those quads replaced. The ratio of
    /// faces to quads is the greedy mesher's compression ratio and is the
    /// single number to watch when tuning it.
    u32 faces = 0;

    [[nodiscard]] bool empty() const { return indices.empty(); }
};

/// Builds a mesh for one chunk, sampling the world across chunk seams so that
/// faces hidden by a neighbouring chunk's voxels are never emitted.
///
/// Pure and thread-safe with respect to a world that is not being written:
/// this is what the job system runs off the main thread.
[[nodiscard]] ChunkMesh greedy_mesh(const VoxelWorld& world, ivec3 chunk_coord);

/// The unmerged face count for a chunk. Only used by tests and the profiler to
/// verify that merging is actually reducing geometry.
[[nodiscard]] u32 count_naive_faces(const VoxelWorld& world, ivec3 chunk_coord);

}  // namespace df
