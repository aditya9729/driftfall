// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "voxel/voxel_world.hpp"

#include "core/log.hpp"

#include <random>

namespace df {

VoxelWorld::VoxelWorld(i32 chunks_x, i32 chunks_y, i32 chunks_z) : chunks_{chunks_x, chunks_y, chunks_z} {
    chunks_map_.reserve(static_cast<usize>(chunks_x * chunks_y * chunks_z));
    for (i32 z = 0; z < chunks_z; ++z) {
        for (i32 y = 0; y < chunks_y; ++y) {
            for (i32 x = 0; x < chunks_x; ++x) {
                // Every chunk starts uniform-empty, so this allocates nothing
                // beyond the map nodes themselves.
                chunks_map_.emplace(ivec3{x, y, z}, Chunk{Voxel::Empty});
            }
        }
    }
}

bool VoxelWorld::contains(ivec3 p) const {
    const ivec3 extent = size_in_voxels();
    return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < extent.x && p.y < extent.y && p.z < extent.z;
}

const Chunk* VoxelWorld::chunk(ivec3 c) const {
    const auto it = chunks_map_.find(c);
    return it == chunks_map_.end() ? nullptr : &it->second;
}

Chunk* VoxelWorld::chunk(ivec3 c) {
    const auto it = chunks_map_.find(c);
    return it == chunks_map_.end() ? nullptr : &it->second;
}

Voxel VoxelWorld::at(i32 x, i32 y, i32 z) const {
    const ivec3 p{x, y, z};
    if (!contains(p)) return Voxel::Empty;
    const Chunk* c = chunk(to_chunk_coord(p));
    if (c == nullptr) return Voxel::Empty;
    const ivec3 l = to_local(p);
    return c->at(l.x, l.y, l.z);
}

void VoxelWorld::mark_dirty_with_neighbours(ivec3 world_voxel) {
    const ivec3 local = to_local(world_voxel);
    const ivec3 owner = to_chunk_coord(world_voxel);

    // A voxel on a chunk face contributes to the neighbouring chunk's mesh
    // too (the mesher samples across the seam to avoid emitting hidden
    // interior faces). Miss this and you get flickering walls at chunk edges.
    for (int axis = 0; axis < 3; ++axis) {
        i32 offset = 0;
        if (local[axis] == 0)
            offset = -1;
        else if (local[axis] == Chunk::kSize - 1)
            offset = 1;
        if (offset == 0) continue;

        ivec3 neighbour = owner;
        neighbour[axis] += offset;
        if (Chunk* n = chunk(neighbour)) n->mark_dirty();
    }
}

void VoxelWorld::set(i32 x, i32 y, i32 z, Voxel v) {
    const ivec3 p{x, y, z};
    if (!contains(p)) return;
    Chunk* c = chunk(to_chunk_coord(p));
    if (c == nullptr) return;

    const ivec3 l = to_local(p);
    const Voxel before = c->at(l.x, l.y, l.z);
    c->set(l.x, l.y, l.z, v);
    if (before != v) mark_dirty_with_neighbours(p);
}

bool VoxelWorld::damage(ivec3 p, u8 amount) {
    if (!contains(p)) return false;
    Chunk* c = chunk(to_chunk_coord(p));
    if (c == nullptr) return false;

    const ivec3 l = to_local(p);
    const bool destroyed = c->damage(l.x, l.y, l.z, amount);
    if (destroyed) mark_dirty_with_neighbours(p);
    return destroyed;
}

std::vector<ivec3> VoxelWorld::take_dirty_chunks() {
    std::vector<ivec3> out;
    for (auto& [coord, c] : chunks_map_) {
        if (c.dirty()) {
            out.push_back(coord);
            c.clear_dirty();
        }
    }
    return out;
}

usize VoxelWorld::heap_bytes() const {
    usize bytes = 0;
    for (const auto& [coord, c] : chunks_map_) bytes += c.heap_bytes() + sizeof(Chunk);
    return bytes;
}

// ---------------------------------------------------------------------------
// Test sector generation
// ---------------------------------------------------------------------------

void generate_test_sector(VoxelWorld& world, u32 seed) {
    const ivec3 extent = world.size_in_voxels();
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99);

    // Outer hull shell: a sealed box, one voxel thick, with the top left open
    // so the sky-box reads as open space when you look up through a breach.
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 x = 0; x < extent.x; ++x) {
            world.set(x, 0, z, Voxel::Bulkhead);  // deck plating
        }
    }
    for (i32 y = 1; y < extent.y; ++y) {
        for (i32 x = 0; x < extent.x; ++x) {
            world.set(x, y, 0, Voxel::HullPlate);
            world.set(x, y, extent.z - 1, Voxel::HullPlate);
        }
        for (i32 z = 0; z < extent.z; ++z) {
            world.set(0, y, z, Voxel::HullPlate);
            world.set(extent.x - 1, y, z, Voxel::HullPlate);
        }
    }

    // Interior rooms on a coarse grid, with doorways punched through so the
    // sector is traversable without mining. Wall height is deliberately short
    // of the ceiling: sightlines over cover are what make a horde arena read.
    constexpr i32 kRoom = 24;
    constexpr i32 kWallHeight = 6;
    for (i32 z = kRoom; z < extent.z - 1; z += kRoom) {
        for (i32 x = 1; x < extent.x - 1; ++x) {
            for (i32 y = 1; y <= kWallHeight; ++y) world.set(x, y, z, Voxel::HullPlate);
        }
    }
    for (i32 x = kRoom; x < extent.x - 1; x += kRoom) {
        for (i32 z = 1; z < extent.z - 1; ++z) {
            for (i32 y = 1; y <= kWallHeight; ++y) world.set(x, y, z, Voxel::HullPlate);
        }
    }

    // Doorways: a 3-wide, 3-tall gap at the midpoint of each wall segment.
    auto punch_door = [&](i32 cx, i32 cz) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            for (i32 dz = -1; dz <= 1; ++dz) {
                for (i32 y = 1; y <= 3; ++y) world.set(cx + dx, y, cz + dz, Voxel::Empty);
            }
        }
    };
    for (i32 z = kRoom; z < extent.z - 1; z += kRoom) {
        for (i32 x = kRoom / 2; x < extent.x - 1; x += kRoom) punch_door(x, z);
    }
    for (i32 x = kRoom; x < extent.x - 1; x += kRoom) {
        for (i32 z = kRoom / 2; z < extent.z - 1; z += kRoom) punch_door(x, z);
    }

    // Ore seams and ice, the mining half of the loop. Sparse on purpose:
    // scarcity is what makes the fortify decision cost something.
    for (i32 z = 2; z < extent.z - 2; ++z) {
        for (i32 x = 2; x < extent.x - 2; ++x) {
            if (pct(rng) < 3) {
                const i32 h = 1 + (pct(rng) % 3);
                const Voxel material = pct(rng) < 70 ? Voxel::Ore : Voxel::Ice;
                for (i32 y = 1; y <= h; ++y) world.set(x, y, z, material);
            }
        }
    }

    log::info("generated sector {}x{}x{} voxels, {} chunks, {:.1f} MB",
              extent.x,
              extent.y,
              extent.z,
              world.chunk_count(),
              static_cast<f64>(world.heap_bytes()) / (1024.0 * 1024.0));
}

}  // namespace df
