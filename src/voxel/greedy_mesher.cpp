// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "voxel/greedy_mesher.hpp"

#include <algorithm>
#include <array>

namespace df {
namespace {

constexpr i32 N = Chunk::kSize;

/// One entry of the sweep plane: which material shows a face here, which way
/// that face points, and how occluded each of its four corners is. Two cells
/// merge only if they are identical, so every field in here is a thing that
/// will split a quad. Keep it small.
struct MaskCell {
    Voxel material = Voxel::Empty;
    i8 dir = 0;  // +1 face points along +axis, -1 along -axis, 0 no face
    u8 damage = 0;
    /// Corner occlusion, 0 (fully enclosed) to 3 (open). Ordered to match the
    /// emitted quad corners: (0,0), (u,0), (u,v), (0,v).
    std::array<u8, 4> ao{3, 3, 3, 3};

    friend bool operator==(const MaskCell&, const MaskCell&) = default;

    [[nodiscard]] bool has_face() const { return dir != 0; }
};

/// The standard voxel corner-occlusion rule (Minecraft's "smooth lighting",
/// described by Mikola Lysenko). `side1` and `side2` are the two voxels
/// sharing an edge with this corner, `corner` the one diagonally across it.
///
/// The early return matters: once both sides are solid the corner is already
/// sealed, and whether the diagonal voxel is there too cannot make it darker.
/// Without it, an inside corner gets a strictly darker pixel where three
/// blocks meet than where two do, which reads as a dirty smudge.
constexpr u8 corner_occlusion(bool side1, bool side2, bool corner) {
    if (side1 && side2) return 0;
    return static_cast<u8>(3 -
                           (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner)));
}

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

    // Occlusion is sampled on the *air* side of a face — the side you can see
    // — from the eight voxels ringing it in the face's own plane. `air` is the
    // empty voxel the face looks into; u_axis and v_axis are the two world axes
    // the face spans.
    const auto corner_ao = [&](const ivec3& air, int u_axis, int v_axis) {
        ivec3 U{0, 0, 0};
        ivec3 V{0, 0, 0};
        U[u_axis] = 1;
        V[v_axis] = 1;

        // Sample the 3x3 neighbourhood once rather than three voxels per
        // corner; the four corners share six of the eight between them.
        bool solid[3][3];
        for (int du = -1; du <= 1; ++du) {
            for (int dv = -1; dv <= 1; ++dv) {
                solid[du + 1][dv + 1] = is_solid(sample(air + U * du + V * dv));
            }
        }

        // Corner order matches the emitted quad: (0,0), (u,0), (u,v), (0,v).
        constexpr std::array<std::array<int, 2>, 4> kCornerDirs{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};

        std::array<u8, 4> out{};
        for (usize c = 0; c < 4; ++c) {
            const int su = kCornerDirs[c][0];
            const int sv = kCornerDirs[c][1];
            out[c] = corner_occlusion(solid[su + 1][1], solid[1][sv + 1], solid[su + 1][sv + 1]);
        }
        return out;
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
                        mask[n] = owner_before_is_outside
                                      ? MaskCell{}
                                      : MaskCell{a, static_cast<i8>(1), damage_of(x), corner_ao(x + q, u, v)};
                    } else {
                        mask[n] =
                            owner_after_is_outside
                                ? MaskCell{}
                                : MaskCell{b, static_cast<i8>(-1), damage_of(x + q), corner_ao(x, u, v)};
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

                    // Every cell in this rectangle carries the same four corner
                    // occlusion values — that is exactly what the merge key
                    // guarantees — so the rectangle inherits them directly.
                    const auto base_index = static_cast<u32>(mesh.vertices.size());
                    const std::array<ivec3, 4> corners{x, x + du, x + du + dv, x + dv};
                    for (usize c = 0; c < corners.size(); ++c) {
                        PackedVertex vert;
                        vert.x = static_cast<u8>(corners[c].x);
                        vert.y = static_cast<u8>(corners[c].y);
                        vert.z = static_cast<u8>(corners[c].z);
                        vert.normal_ao = static_cast<u8>((normal_index << 2) | cell.ao[c]);
                        vert.material = static_cast<u8>(cell.material);
                        vert.damage = cell.damage;
                        mesh.vertices.push_back(vert);
                    }

                    // Splitting a quad into two triangles interpolates the
                    // corner values anisotropically: a value on the shared
                    // diagonal reaches across the whole quad, one off it does
                    // not. With unequal corners that shows up as a hard crease
                    // running corner to corner. Putting the diagonal across the
                    // *darker* pair keeps the gradient smooth.
                    const bool flip = (cell.ao[0] + cell.ao[2]) > (cell.ao[1] + cell.ao[3]);

                    if (cell.dir > 0) {
                        if (flip) {
                            mesh.indices.insert(mesh.indices.end(),
                                                {base_index + 1,
                                                 base_index + 2,
                                                 base_index + 3,
                                                 base_index + 1,
                                                 base_index + 3,
                                                 base_index + 0});
                        } else {
                            mesh.indices.insert(mesh.indices.end(),
                                                {base_index + 0,
                                                 base_index + 1,
                                                 base_index + 2,
                                                 base_index + 0,
                                                 base_index + 2,
                                                 base_index + 3});
                        }
                    } else {
                        if (flip) {
                            mesh.indices.insert(mesh.indices.end(),
                                                {base_index + 1,
                                                 base_index + 3,
                                                 base_index + 2,
                                                 base_index + 1,
                                                 base_index + 0,
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
