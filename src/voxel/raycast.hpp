// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "voxel/voxel_world.hpp"

namespace df {

struct VoxelHit {
    /// The first solid voxel the ray entered.
    ivec3 voxel{0, 0, 0};

    /// Outward normal of the face the ray came in through — the face you would
    /// stick a barricade to. Zero when the ray began inside a solid voxel,
    /// because then it never crossed a face.
    ivec3 normal{0, 0, 0};

    /// Distance along the ray to that face, in voxels.
    f32 distance = 0.0f;

    bool hit = false;

    explicit operator bool() const { return hit; }
};

/// Walks the voxel grid along a ray and returns the first solid voxel.
///
/// Amanatides & Woo: step from voxel to voxel along the ray rather than
/// sampling it at fixed intervals. Fixed-step sampling is the tempting version
/// and it is wrong in both directions at once — it skips thin walls when the
/// step is coarse, and burns the budget re-sampling the same voxel when it is
/// fine. This visits every voxel the ray touches, exactly once, in order.
///
/// `direction` need not be normalised; `max_distance` is measured in voxels
/// along the normalised ray. A ray that starts outside the sector simply finds
/// nothing, because out-of-bounds reads come back Empty.
[[nodiscard]] VoxelHit raycast_voxels(const VoxelWorld& world, vec3 origin, vec3 direction, f32 max_distance);

}  // namespace df
