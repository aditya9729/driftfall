// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "game/physics.hpp"

#include <cassert>
#include <cmath>

namespace df {
namespace {

/// Backstop on the per-axis plane walk, in the spirit of raycast.cpp's
/// kMaxSteps. Under the one-voxel invariant the walk body runs at most once,
/// so this only ever bounds a caller that has already broken its contract.
constexpr i32 kMaxPlanesPerAxis = 160;

/// X, then Z, then Y. Vertical last so grounding reflects where the box ended
/// up horizontally, not where it started.
constexpr int kAxisOrder[3] = {0, 2, 1};

/// The two axes spanning the cross-section swept along `axis`.
constexpr int kCrossA[3] = {1, 2, 0};
constexpr int kCrossB[3] = {2, 0, 1};

i32 floor_to_int(f32 v) {
    return static_cast<i32>(std::floor(v));
}

i32 ceil_to_int(f32 v) {
    return static_cast<i32>(std::ceil(v));
}

bool all_finite(vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

/// Cell indices the interval [lo, hi] occupies. `hi` exactly on a boundary
/// excludes the cell beyond it, `lo` exactly on a boundary includes the cell
/// beyond it — the two halves of "touching is not overlapping".
i32 first_cell(f32 lo) {
    return floor_to_int(lo);
}

i32 last_cell(f32 hi) {
    return ceil_to_int(hi) - 1;
}

/// Is any solid voxel in the `axis`-normal plane at index `plane`, within the
/// box's cross-section?
bool plane_blocked(const VoxelWorld& world, int axis, i32 plane, const vec3& lo, const vec3& hi) {
    const int a = kCrossA[axis];
    const int b = kCrossB[axis];
    const i32 a0 = first_cell(lo[a]);
    const i32 a1 = last_cell(hi[a]);
    const i32 b0 = first_cell(lo[b]);
    const i32 b1 = last_cell(hi[b]);

    // One at() per cell. A character cross-section is 1-2 cells on a side, so
    // this is a handful of reads; if the 220-enemy loop ever shows up in a
    // profile, the fix is to hoist the chunk lookup out of the inner pair,
    // not to sample the box more coarsely.
    ivec3 cell{0, 0, 0};
    cell[axis] = plane;
    for (i32 j = a0; j <= a1; ++j) {
        cell[a] = j;
        for (i32 k = b0; k <= b1; ++k) {
            cell[b] = k;
            if (is_solid(world.at(cell))) return true;
        }
    }
    return false;
}

struct AxisMove {
    f32 travelled = 0.0f;
    bool blocked = false;
};

/// Resolves motion of `delta` along one axis.
///
/// Only planes the box has not already entered are consulted. That single
/// choice is what stops a penetrating box from being welded in place: the
/// voxels it is already inside cannot block it, so it can always move out of
/// them, and the newly-entered planes stop it from moving further in.
AxisMove resolve_axis(const VoxelWorld& world, int axis, const vec3& lo, const vec3& hi, f32 delta) {
    AxisMove out;
    if (delta == 0.0f) return out;

    if (delta > 0.0f) {
        const f32 face = hi[axis];
        // Cell i is entered once the leading face passes i, so the first
        // not-yet-entered plane is ceil(face) and the last one the move would
        // reach is ceil(face + delta) - 1.
        const i32 first = ceil_to_int(face);
        const i32 last = ceil_to_int(face + delta) - 1;
        i32 examined = 0;
        for (i32 i = first; i <= last; ++i) {
            if (++examined > kMaxPlanesPerAxis || plane_blocked(world, axis, i, lo, hi)) {
                const f32 stop = static_cast<f32>(i) - kContactSkin;
                out.travelled = stop > face ? stop - face : 0.0f;
                out.blocked = true;
                return out;
            }
        }
        out.travelled = delta;
        return out;
    }

    const f32 face = lo[axis];
    const i32 first = floor_to_int(face) - 1;
    const i32 last = floor_to_int(face + delta);
    i32 examined = 0;
    for (i32 i = first; i >= last; --i) {
        if (++examined > kMaxPlanesPerAxis || plane_blocked(world, axis, i, lo, hi)) {
            const f32 stop = static_cast<f32>(i + 1) + kContactSkin;
            out.travelled = stop < face ? stop - face : 0.0f;
            out.blocked = true;
            return out;
        }
    }
    out.travelled = delta;
    return out;
}

/// Bounding box of the solid cells the box overlaps.
struct SolidSpan {
    bool any = false;
    ivec3 lo{0, 0, 0};
    ivec3 hi{0, 0, 0};
};

SolidSpan overlapped_solids(const VoxelWorld& world, const vec3& lo, const vec3& hi) {
    SolidSpan span;
    const i32 x1 = last_cell(hi.x);
    const i32 y1 = last_cell(hi.y);
    const i32 z1 = last_cell(hi.z);

    for (i32 x = first_cell(lo.x); x <= x1; ++x) {
        for (i32 y = first_cell(lo.y); y <= y1; ++y) {
            for (i32 z = first_cell(lo.z); z <= z1; ++z) {
                if (!is_solid(world.at(x, y, z))) continue;
                const ivec3 cell{x, y, z};
                if (!span.any) {
                    span.any = true;
                    span.lo = cell;
                    span.hi = cell;
                } else {
                    for (int a = 0; a < 3; ++a) {
                        if (cell[a] < span.lo[a]) span.lo[a] = cell[a];
                        if (cell[a] > span.hi[a]) span.hi[a] = cell[a];
                    }
                }
            }
        }
    }
    return span;
}

/// Candidate escape directions, in tie-break order: up first, then down, then
/// the horizontal pairs. Fixed order is what keeps a symmetrically buried box
/// choosing the same way on every machine and every replay.
constexpr int kEjectAxis[6] = {1, 1, 0, 0, 2, 2};
constexpr f32 kEjectSign[6] = {1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f};

bool half_extents_valid(vec3 h) {
    return all_finite(h) && h.x > 0.0f && h.y > 0.0f && h.z > 0.0f && h.x <= kMaxHalfExtent &&
           h.y <= kMaxHalfExtent && h.z <= kMaxHalfExtent;
}

}  // namespace

bool box_overlaps_solid(const VoxelWorld& world, vec3 centre, vec3 half_extents) {
    if (!all_finite(centre) || !half_extents_valid(half_extents)) return false;

    const vec3 lo = centre - half_extents;
    const vec3 hi = centre + half_extents;
    const i32 x1 = last_cell(hi.x);
    const i32 y1 = last_cell(hi.y);
    const i32 z1 = last_cell(hi.z);

    for (i32 x = first_cell(lo.x); x <= x1; ++x) {
        for (i32 y = first_cell(lo.y); y <= y1; ++y) {
            for (i32 z = first_cell(lo.z); z <= z1; ++z) {
                if (is_solid(world.at(x, y, z))) return true;
            }
        }
    }
    return false;
}

bool box_grounded(const VoxelWorld& world, vec3 centre, vec3 half_extents) {
    if (!all_finite(centre) || !half_extents_valid(half_extents)) return false;

    // Phrased as a downward probe move rather than a slab test so it inherits
    // resolve_axis's "already-entered cells do not count" rule for free: a box
    // buried in a floor is grounded because of the voxel below it, not because
    // of the one it is standing inside.
    const vec3 lo = centre - half_extents;
    const vec3 hi = centre + half_extents;
    return resolve_axis(world, 1, lo, hi, -kGroundProbe).blocked;
}

DepenetrationResult resolve_penetration(const VoxelWorld& world,
                                        vec3 position,
                                        vec3 half_extents,
                                        f32 max_eject) {
    DepenetrationResult out;
    out.position = position;
    if (!all_finite(position) || !half_extents_valid(half_extents)) return out;

    const vec3 lo = position - half_extents;
    const vec3 hi = position + half_extents;
    const SolidSpan span = overlapped_solids(world, lo, hi);
    if (!span.any) {
        out.resolved = true;
        return out;
    }
    out.penetrating = true;

    // Distances that clear the whole overlapped span, not just its nearest
    // cell. Using the span's far face is the difference between escaping a
    // wall and escaping the first voxel of one and stopping inside the second.
    f32 distance[6];
    bool clears[6];
    for (int k = 0; k < 6; ++k) {
        const int a = kEjectAxis[k];
        distance[k] = kEjectSign[k] > 0.0f ? (static_cast<f32>(span.hi[a] + 1) + kContactSkin) - lo[a]
                                           : hi[a] - (static_cast<f32>(span.lo[a]) - kContactSkin);
        vec3 destination = position;
        destination[a] += kEjectSign[k] * distance[k];
        clears[k] = !box_overlaps_solid(world, destination, half_extents);
    }

    int chosen = -1;
    // Up wins outright when it is cheap enough, even against a shorter
    // sideways push. Standing on the block you just placed is the expected
    // outcome, and up is the one direction that cannot deposit you through a
    // wall in a sealed room.
    if (clears[0] && distance[0] <= 2.0f * half_extents.y) {
        chosen = 0;
    } else {
        for (int k = 0; k < 6; ++k) {
            // Strict less-than, so ties fall to the earlier direction and the
            // choice stays a pure function of the geometry.
            if (clears[k] && (chosen < 0 || distance[k] < distance[chosen])) chosen = k;
        }
    }

    if (chosen < 0) {
        // Sealed. Deliberately no fallback: shoving toward the shallowest face
        // anyway would tunnel the player through the Warden's new wall, which
        // is precisely the threat the encounter is built on.
        out.sealed = true;
        return out;
    }

    if (!(max_eject > 0.0f)) return out;
    const f32 budget = std::isfinite(max_eject) ? max_eject : kInstantEject;
    const f32 step = distance[chosen] < budget ? distance[chosen] : budget;

    // A partial push shortens the chosen direction's remaining distance and
    // leaves the other five unchanged, so the same direction stays the winner
    // on the next call. That is what stops a rate-limited escape from
    // oscillating between two faces and never leaving.
    out.position[kEjectAxis[chosen]] += kEjectSign[chosen] * step;
    out.resolved = !box_overlaps_solid(world, out.position, half_extents);
    return out;
}

SweepResult move_and_slide(
    const VoxelWorld& world, vec3 position, vec3 half_extents, vec3 displacement, vec3 velocity) {
    SweepResult out;
    out.position = position;
    out.velocity = velocity;

    if (!all_finite(position) || !half_extents_valid(half_extents)) return out;
    if (!all_finite(displacement) || !all_finite(velocity)) {
        out.velocity = vec3{0.0f};
        return out;
    }

    assert(std::fabs(displacement.x) < kMaxAxisDisplacement &&
           std::fabs(displacement.y) < kMaxAxisDisplacement &&
           std::fabs(displacement.z) < kMaxAxisDisplacement &&
           "move_and_slide: per-axis displacement must stay under one voxel");

    vec3 centre = position;
    vec3 resolved = velocity;
    bool* const hit[3] = {&out.hit_x, &out.hit_y, &out.hit_z};

    for (const int axis : kAxisOrder) {
        const vec3 lo = centre - half_extents;
        const vec3 hi = centre + half_extents;
        const AxisMove move = resolve_axis(world, axis, lo, hi, displacement[axis]);
        centre[axis] += move.travelled;
        if (move.blocked) {
            *hit[axis] = true;
            // Zeroed even when travelled is 0 — pressing into a wall you are
            // already flush against is a contact. Skipping it is how gravity
            // accumulates while you stand still.
            resolved[axis] = 0.0f;
        }
    }

    const ivec3 size = world.size_in_voxels();
    const vec3 extent{static_cast<f32>(size.x), static_cast<f32>(size.y), static_cast<f32>(size.z)};
    for (int axis = 0; axis < 3; ++axis) {
        if (centre[axis] < 0.0f || centre[axis] > extent[axis]) out.out_of_bounds = true;

        const f32 min_centre = -kGuardMargin;
        const f32 max_centre = extent[axis] + kGuardMargin;
        if (centre[axis] < min_centre) {
            centre[axis] = min_centre;
            if (resolved[axis] < 0.0f) resolved[axis] = 0.0f;
        } else if (centre[axis] > max_centre) {
            centre[axis] = max_centre;
            if (resolved[axis] > 0.0f) resolved[axis] = 0.0f;
        }
    }

    out.position = centre;
    out.velocity = resolved;
    out.grounded = box_grounded(world, centre, half_extents);
    return out;
}

}  // namespace df
