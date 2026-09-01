// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "voxel/raycast.hpp"

#include <cmath>
#include <limits>

namespace df {
namespace {

/// A ray can only cross so many voxel boundaries before max_distance stops it,
/// but a denormal direction could in principle stall the walk. This is the
/// belt-and-braces bound: a shot can never cost more than this many steps.
constexpr i32 kMaxSteps = 4096;

i32 floor_to_int(f32 v) {
    return static_cast<i32>(std::floor(v));
}

}  // namespace

VoxelHit raycast_voxels(const VoxelWorld& world, vec3 origin, vec3 direction, f32 max_distance) {
    VoxelHit result;

    const f32 length =
        std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (!std::isfinite(length) || length < 1.0e-6f) return result;
    if (!(max_distance > 0.0f)) return result;

    const vec3 dir = direction / length;

    ivec3 voxel{floor_to_int(origin.x), floor_to_int(origin.y), floor_to_int(origin.z)};

    // Starting inside something solid counts as an immediate hit. The muzzle
    // being buried in a wall is a real case once the player can stand against
    // one, and reporting a miss there would let shots pass through it.
    if (is_solid(world.at(voxel))) {
        result.voxel = voxel;
        result.distance = 0.0f;
        result.hit = true;
        return result;
    }

    constexpr f32 kInf = std::numeric_limits<f32>::infinity();

    ivec3 step{0, 0, 0};
    vec3 t_max{kInf, kInf, kInf};
    vec3 t_delta{kInf, kInf, kInf};

    for (int axis = 0; axis < 3; ++axis) {
        const f32 d = dir[axis];
        if (d > 0.0f) {
            step[axis] = 1;
            t_delta[axis] = 1.0f / d;
            // Distance to the next boundary above the origin.
            t_max[axis] = (static_cast<f32>(voxel[axis] + 1) - origin[axis]) / d;
        } else if (d < 0.0f) {
            step[axis] = -1;
            t_delta[axis] = 1.0f / -d;
            t_max[axis] = (origin[axis] - static_cast<f32>(voxel[axis])) / -d;
        }
        // d == 0: the ray never crosses a boundary on this axis, so both stay
        // infinite and this axis is never chosen as the next step.
    }

    for (i32 i = 0; i < kMaxSteps; ++i) {
        // Advance across whichever boundary is nearest.
        int axis = 0;
        if (t_max.y < t_max.x) axis = 1;
        if (t_max[2] < t_max[axis]) axis = 2;

        const f32 travelled = t_max[axis];
        if (!(travelled <= max_distance)) return result;

        voxel[axis] += step[axis];
        t_max[axis] += t_delta[axis];

        if (is_solid(world.at(voxel))) {
            result.voxel = voxel;
            result.distance = travelled;
            // We entered through the face facing back along the step.
            result.normal[axis] = -step[axis];
            result.hit = true;
            return result;
        }
    }

    return result;
}

}  // namespace df
