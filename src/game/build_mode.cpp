// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "game/build_mode.hpp"

#include "game/sim.hpp"  // build_cost
#include "voxel/raycast.hpp"

#include <cmath>

namespace df {
namespace {

bool finite(vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

/// The guard every entry point runs first. raycast_voxels already refuses a
/// zero or non-finite direction and a non-positive distance, but it floors the
/// origin straight into an i32 without checking it, so a NaN position would be
/// undefined behaviour rather than a miss. Checking all three here keeps the
/// contract readable at the call site instead of spread across two files.
bool ray_is_usable(vec3 origin, vec3 direction, f32 reach) {
    if (!finite(origin) || !finite(direction)) return false;
    if (!(reach > 0.0f)) return false;  // also rejects NaN
    return direction.x != 0.0f || direction.y != 0.0f || direction.z != 0.0f;
}

/// Overlap on one axis, half-open cell against closed body. Strict on both
/// sides: flush is not inside.
bool axis_overlaps(i32 cell, f32 centre, f32 half) {
    const f32 lo = static_cast<f32>(cell);
    const f32 hi = static_cast<f32>(cell + 1);
    return centre - half < hi && centre + half > lo;
}

}  // namespace

BuildCursor compute_build_cursor(
    const VoxelWorld& world, vec3 origin, vec3 direction, f32 reach, Voxel material, i32 salvage) {
    BuildCursor cursor;

    // Price first: an unbuildable material has no cursor at all, and reporting
    // cost 0 for it stops the HUD from offering the player a free wall.
    cursor.cost = build_cost(material);
    if (cursor.cost <= 0) return cursor;

    if (!ray_is_usable(origin, direction, reach)) return cursor;

    const VoxelHit hit = raycast_voxels(world, origin, direction, reach);
    if (!hit) return cursor;

    // A zero normal means the ray started buried in solid geometry, so it never
    // crossed a face. There is no side to attach to, and guessing one would put
    // the block somewhere the player did not point at.
    if (hit.normal == ivec3{0, 0, 0}) return cursor;

    const ivec3 target = hit.voxel + hit.normal;

    // Outside the sector is refused rather than clamped. VoxelWorld::set()
    // silently drops out-of-range writes, so clamping would charge the player
    // for a voxel that never appears.
    if (!world.contains(target)) return cursor;

    // The DDA guarantees this cell was empty one step ago, so this cannot fire
    // today. It stays because the guarantee lives in raycast.cpp, not here, and
    // the cost of not depending on someone else's loop invariant is one read.
    if (is_solid(world.at(target))) return cursor;

    cursor.target = target;
    cursor.anchor = hit.voxel;
    cursor.valid = true;
    cursor.affordable = salvage >= cursor.cost;
    return cursor;
}

bool cell_intersects_body(ivec3 cell, vec3 centre, vec3 half_extents) {
    // Refuse rather than resolve. See the header: an undescribable body blocks
    // the build instead of licensing it.
    if (!finite(centre) || !finite(half_extents)) return true;
    if (half_extents.x < 0.0f || half_extents.y < 0.0f || half_extents.z < 0.0f) return true;

    return axis_overlaps(cell.x, centre.x, half_extents.x) &&
           axis_overlaps(cell.y, centre.y, half_extents.y) && axis_overlaps(cell.z, centre.z, half_extents.z);
}

bool turret_site_clear(const VoxelWorld& world, ivec3 base) {
    const ivec3 floor_cell{base.x, base.y - 1, base.z};
    if (!world.contains(floor_cell)) return false;
    if (!is_solid(world.at(floor_cell))) return false;

    for (i32 dy = 0; dy < kTurretHeightVoxels; ++dy) {
        const ivec3 cell{base.x, base.y + dy, base.z};
        if (!world.contains(cell)) return false;
        if (is_solid(world.at(cell))) return false;
    }
    return true;
}

TurretSite compute_turret_site(
    const VoxelWorld& world, vec3 origin, vec3 direction, f32 reach, i32 cost, i32 salvage) {
    TurretSite site;
    site.cost = cost;

    if (!ray_is_usable(origin, direction, reach)) return site;

    const VoxelHit hit = raycast_voxels(world, origin, direction, reach);
    if (!hit) return site;

    // Floors only. A wall or ceiling hit is a miss, not a nearby-deck search.
    if (hit.normal.x != 0 || hit.normal.y != 1 || hit.normal.z != 0) return site;

    const ivec3 base = hit.voxel + hit.normal;
    if (!turret_site_clear(world, base)) return site;

    site.base = base;
    site.anchor = hit.voxel;
    // Centred in the column's footprint and lifted by half the turret's height,
    // so `position` is the box centre kTurretHalfExtents is measured from —
    // the same convention move_and_slide and resolve_penetration use.
    site.position = vec3{static_cast<f32>(base.x) + 0.5f,
                         static_cast<f32>(base.y) + static_cast<f32>(kTurretHeightVoxels) * 0.5f,
                         static_cast<f32>(base.z) + 0.5f};
    site.valid = true;
    site.affordable = cost > 0 && salvage >= cost;
    return site;
}

}  // namespace df
