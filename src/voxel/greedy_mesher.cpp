// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "voxel/greedy_mesher.hpp"

#include <algorithm>
#include <array>

namespace df {
namespace {

constexpr i32 N = Chunk::kSize;

/// One entry of the sweep plane: which material shows a face here, and which
/// way that face points. Two cells merge only if they are identical, so every
/// field in here is a thing that will split a quad. Keep it small.
struct MaskCell {
    Voxel material = Voxel::Empty;
    i8 dir = 0;  // +1 face points along +axis, -1 along -axis, 0 no face
    u8 damage = 0;

    friend bool operator==(const MaskCell&, const MaskCell&) = default;

    [[nodiscard]] bool has_face() const { return dir != 0; }
};

}  // namespace

ChunkMesh greedy_mesh(const VoxelWorld& world, ivec3 chunk_coord) {
    ChunkMesh mesh;

    const Chunk* self = world.chunk(chunk_coord);
    if (self == nullptr || self->is_empty()) return mesh;

    const ivec3 base = chunk_coord * N;

    // Sampling goes through the world, not the chunk, so a face buried behind
    // a neighbouring chunk's voxel is never emitted. Out-of-sector reads come
    // back Empty, which correctly exposes the sector's outer hull.
    const auto sample = [&](const ivec3& local) { return world.at(base + local); };

    // Damage normalised to 0..255 so the shader does not need the material's
    // toughness table.
    const auto damage_of = [&](const ivec3& local) -> u8 {
        const ivec3 wp = base + local;
        if (!world.contains(wp)) return 0;
        const Chunk* c = world.chunk(VoxelWorld::to_chunk_coord(wp));
        if (c == nullptr) return 0;
        const ivec3 l = VoxelWorld::to_local(wp);
        const u8 raw = c->damage_at(l.x, l.y, l.z);
        if (raw == 0) return 0;
        const u8 toughness = voxel_toughness(c->at(l.x, l.y, l.z));
        if (toughness == 0) return 0;
        return static_cast<u8>(std::min<u32>(255u, static_cast<u32>(raw) * 255u / toughness));
    };

    std::array<MaskCell, static_cast<usize>(N) * N> mask{};

    // Sweep each of the three axes. For axis d, we walk N+1 planes (including
    // one before the chunk and one after) and for each plane build a mask of
    // the faces visible on it, then merge that mask into as few rectangles as
    // possible. This is Lysenko's greedy meshing, with material and damage
    // folded into the merge key.
    for (int d = 0; d < 3; ++d) {
        const int u = (d + 1) % 3;
        const int v = (d + 2) % 3;

        ivec3 x{0, 0, 0};
        ivec3 q{0, 0, 0};
        q[d] = 1;

        for (x[d] = -1; x[d] < N;) {
            // A face belongs to the voxel it grows out of. On the two planes
            // that straddle a chunk boundary, one side's owner lives in the
            // neighbouring chunk — and that neighbour will emit the face
            // itself. Skipping it here is what stops every seam from being
            // drawn twice, which otherwise ships as z-fighting shimmer along
            // every chunk edge.
            const bool owner_before_is_outside = (x[d] < 0);      // dir +1 owner
            const bool owner_after_is_outside = (x[d] >= N - 1);  // dir -1 owner

            // --- build the mask for this plane -------------------------------
            usize n = 0;
            for (x[v] = 0; x[v] < N; ++x[v]) {
                for (x[u] = 0; x[u] < N; ++x[u], ++n) {
                    const Voxel a = sample(x);
                    const Voxel b = sample(x + q);
                    const bool solid_a = is_solid(a);
                    const bool solid_b = is_solid(b);

                    if (solid_a == solid_b) {
                        // Both solid: the face is interior, nobody sees it.
                        // Both empty: there is nothing to draw.
                        mask[n] = MaskCell{};
                    } else if (solid_a) {
                        mask[n] = owner_before_is_outside ? MaskCell{}
                                                          : MaskCell{a, static_cast<i8>(1), damage_of(x)};
                    } else {
                        mask[n] = owner_after_is_outside ? MaskCell{}
                                                         : MaskCell{b, static_cast<i8>(-1), damage_of(x + q)};
                    }
                }
            }

            ++x[d];  // the face plane sits between slice d-1 and slice d

            // --- merge the mask into rectangles ------------------------------
            n = 0;
            for (i32 j = 0; j < N; ++j) {
                for (i32 i = 0; i < N;) {
                    const MaskCell cell = mask[n];
                    if (!cell.has_face()) {
                        ++i;
                        ++n;
                        continue;
                    }

                    // Grow right as far as identical cells allow...
                    i32 w = 1;
                    while (i + w < N && mask[n + static_cast<usize>(w)] == cell) ++w;

                    // ...then grow down, but only in full rows of width w.
                    i32 h = 1;
                    bool row_matches = true;
                    while (j + h < N && row_matches) {
                        for (i32 k = 0; k < w; ++k) {
                            if (!(mask[n + static_cast<usize>(k) + static_cast<usize>(h) * N] == cell)) {
                                row_matches = false;
                                break;
                            }
                        }
                        if (row_matches) ++h;
                    }

                    x[u] = i;
                    x[v] = j;

                    ivec3 du{0, 0, 0};
                    ivec3 dv{0, 0, 0};
                    du[u] = w;
                    dv[v] = h;

                    // (d+1, d+2) is a right-handed frame for every d, so
                    // du x dv always points along +d. That is what makes one
                    // winding rule work for all three axes.
                    const u8 normal_index = static_cast<u8>(d * 2 + (cell.dir > 0 ? 0 : 1));

                    // Ambient occlusion is deliberately flat here. Per-vertex AO
                    // has to be part of the merge key or quads with differing
                    // corner occlusion get merged and the shading tears. Folding
                    // it in roughly triples the quad count, so AO is a screen-space
                    // pass in the renderer instead (see docs/ARCHITECTURE.md).
                    constexpr u8 kAoFull = 3;
                    const u8 normal_ao = static_cast<u8>((normal_index << 2) | kAoFull);

                    const auto base_index = static_cast<u32>(mesh.vertices.size());
                    const std::array<ivec3, 4> corners{x, x + du, x + du + dv, x + dv};
                    for (const ivec3& corner : corners) {
                        PackedVertex vert;
                        vert.x = static_cast<u8>(corner.x);
                        vert.y = static_cast<u8>(corner.y);
                        vert.z = static_cast<u8>(corner.z);
                        vert.normal_ao = normal_ao;
                        vert.material = static_cast<u8>(cell.material);
                        vert.damage = cell.damage;
                        mesh.vertices.push_back(vert);
                    }

                    if (cell.dir > 0) {
                        mesh.indices.insert(mesh.indices.end(),
                                            {base_index + 0,
                                             base_index + 1,
                                             base_index + 2,
                                             base_index + 0,
                                             base_index + 2,
                                             base_index + 3});
                    } else {
                        mesh.indices.insert(mesh.indices.end(),
                                            {base_index + 0,
                                             base_index + 2,
                                             base_index + 1,
                                             base_index + 0,
                                             base_index + 3,
                                             base_index + 2});
                    }

                    ++mesh.quads;
                    mesh.faces += static_cast<u32>(w * h);

                    // Consume the rectangle so it is not emitted again.
                    for (i32 l = 0; l < h; ++l) {
                        for (i32 k = 0; k < w; ++k) {
                            mask[n + static_cast<usize>(k) + static_cast<usize>(l) * N] = MaskCell{};
                        }
                    }

                    i += w;
                    n += static_cast<usize>(w);
                }
            }
        }
    }

    return mesh;
}

u32 count_naive_faces(const VoxelWorld& world, ivec3 chunk_coord) {
    const Chunk* self = world.chunk(chunk_coord);
    if (self == nullptr || self->is_empty()) return 0;

    const ivec3 base = chunk_coord * N;
    constexpr std::array<ivec3, 6> kNeighbours{
        ivec3{1, 0, 0}, ivec3{-1, 0, 0}, ivec3{0, 1, 0}, ivec3{0, -1, 0}, ivec3{0, 0, 1}, ivec3{0, 0, -1}};

    u32 faces = 0;
    for (i32 z = 0; z < N; ++z) {
        for (i32 y = 0; y < N; ++y) {
            for (i32 x = 0; x < N; ++x) {
                const ivec3 local{x, y, z};
                if (!is_solid(world.at(base + local))) continue;
                for (const ivec3& offset : kNeighbours) {
                    if (!is_solid(world.at(base + local + offset))) ++faces;
                }
            }
        }
    }
    return faces;
}

}  // namespace df
