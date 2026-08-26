// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "voxel/chunk.hpp"

#include <unordered_map>
#include <vector>

namespace df {

/// A single derelict station sector.
///
/// Deliberately *bounded*, not infinite. This is the most important scoping
/// decision in the project: an infinite world means streaming, LOD, an origin
/// rebase for float precision, and a save format that grows without limit.
/// A station sector is roughly 224 x 64 x 224 voxels, fits in memory whole,
/// serialises in one shot, and is exactly as much space as a ten-minute
/// horde run needs.
class VoxelWorld {
public:
    /// Sector extent measured in chunks.
    VoxelWorld(i32 chunks_x, i32 chunks_y, i32 chunks_z);

    [[nodiscard]] ivec3 size_in_chunks() const { return chunks_; }

    [[nodiscard]] ivec3 size_in_voxels() const { return chunks_ * Chunk::kSize; }

    /// World-space voxel read. Anything outside the sector reads as Empty,
    /// which makes the sector boundary behave like open vacuum.
    [[nodiscard]] Voxel at(i32 x, i32 y, i32 z) const;

    [[nodiscard]] Voxel at(ivec3 p) const { return at(p.x, p.y, p.z); }

    void set(i32 x, i32 y, i32 z, Voxel v);

    void set(ivec3 p, Voxel v) { set(p.x, p.y, p.z, v); }

    /// Applies damage to a voxel. Returns true if it was destroyed. Marks the
    /// owning chunk dirty, plus any neighbour whose mesh borders this voxel.
    bool damage(ivec3 p, u8 amount);

    [[nodiscard]] const Chunk* chunk(ivec3 chunk_coord) const;

    [[nodiscard]] Chunk* chunk(ivec3 chunk_coord);

    /// Chunk coordinates whose meshes are stale, ordered by insertion. The
    /// renderer drains this under a per-frame budget rather than all at once.
    [[nodiscard]] std::vector<ivec3> take_dirty_chunks();

    [[nodiscard]] usize heap_bytes() const;

    [[nodiscard]] usize chunk_count() const { return chunks_map_.size(); }

    [[nodiscard]] bool contains(ivec3 p) const;

    static constexpr ivec3 to_chunk_coord(ivec3 world) {
        return {floor_div(world.x, Chunk::kSize),
                floor_div(world.y, Chunk::kSize),
                floor_div(world.z, Chunk::kSize)};
    }

    static constexpr ivec3 to_local(ivec3 world) {
        return {mod_floor(world.x, Chunk::kSize),
                mod_floor(world.y, Chunk::kSize),
                mod_floor(world.z, Chunk::kSize)};
    }

private:
    static constexpr i32 floor_div(i32 a, i32 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

    static constexpr i32 mod_floor(i32 a, i32 b) {
        const i32 r = a % b;
        return r < 0 ? r + b : r;
    }

    struct ChunkHash {
        usize operator()(const ivec3& c) const noexcept {
            // Chunk coords are small; a cheap mix beats a general-purpose hash.
            return (static_cast<usize>(static_cast<u32>(c.x)) * 73856093u) ^
                   (static_cast<usize>(static_cast<u32>(c.y)) * 19349663u) ^
                   (static_cast<usize>(static_cast<u32>(c.z)) * 83492791u);
        }
    };

    struct ChunkEq {
        bool operator()(const ivec3& a, const ivec3& b) const noexcept {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }
    };

    void mark_dirty_with_neighbours(ivec3 world_voxel);

    ivec3 chunks_{0};
    std::unordered_map<ivec3, Chunk, ChunkHash, ChunkEq> chunks_map_;
};

/// Fills a sector with a deterministic test station: outer hull, a floor grid,
/// interior bulkhead rooms, and scattered ore. Deterministic in `seed` so a
/// run can be reproduced exactly from its seed — which is also how the replay
/// and daily-run features will work later.
void generate_test_sector(VoxelWorld& world, u32 seed);

}  // namespace df
