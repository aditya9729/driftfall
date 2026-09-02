// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "voxel/voxel_world.hpp"

#include <vector>

namespace df {

/// A coarse flow field over the sector: "from here, step this way to reach the
/// player".
///
/// Steering is not pathfinding, and the difference is a soft-lock. The
/// generated sector partitions into rooms with full-height walls and a handful
/// of doorways (see generate_test_sector), so an enemy that simply drives at
/// the player pins against the first wall it meets and stays there. The wave
/// then never clears, `Sim::enemies_remaining()` never reaches zero, and the
/// Assault phase hangs — headlessly, deterministically, in the test suite.
///
/// A flow field rather than per-agent A*: every enemy shares one goal, so the
/// cost of a single breadth-first sweep from the player is amortised across the
/// whole horde instead of paid 220 times. It also degrades honestly — a cell
/// the sweep never reached reports unreachable, which is information the caller
/// can act on, rather than a path that quietly does not exist.
///
/// Coarse on purpose. At kCellSize the field over a default sector is a few
/// thousand cells, which rebuilds inside the frame budget; a per-voxel field
/// would be ~3 million and could not.
class NavField {
public:
    /// Voxels per field cell along each axis. Four is the largest cell that
    /// still resolves a doorway: openings are punched four voxels wide, so a
    /// coarser grid would close them and wall the sector off again.
    static constexpr i32 kCellSize = 4;

    NavField() = default;

    /// Sizes the field to a sector. Does not populate it; call rebuild().
    void reset(ivec3 world_size_voxels);

    /// Recomputes the field with `goal` (a world voxel coordinate) as the sink.
    /// Cells are passable if a body of `half_extents` voxels fits there, so the
    /// field a Bulwark follows is not necessarily the one a Skitter follows.
    void rebuild(const VoxelWorld& world, ivec3 goal, vec3 half_extents);

    /// Unit direction toward the goal from a world position, or a zero vector.
    ///
    /// Zero means one of three things, and the third is easy to miss: the
    /// position is outside the field, the sweep never reached its cell, or it
    /// *is* the goal cell, which has nowhere downhill to point. The obvious
    /// reading of zero as "unreachable" is therefore wrong at exactly the
    /// moment an enemy arrives. Use reachable() to tell them apart, and never
    /// normalise the result.
    [[nodiscard]] vec3 direction_at(vec3 world_position) const;

    /// Whether the goal is reachable from here at all. This is what lets Sim
    /// detect a wave that can never arrive instead of hanging on it.
    [[nodiscard]] bool reachable(vec3 world_position) const;

    /// Distance to the goal in cells, or -1 when unreachable. Exposed for
    /// tests, which assert connectivity of the generated sector, and for the
    /// anti-soft-lock rule, which watches it stop decreasing.
    [[nodiscard]] i32 distance_at(vec3 world_position) const;

    [[nodiscard]] ivec3 size_in_cells() const { return cells_; }

    /// Cells the last rebuild could reach. A generated sector whose reachable
    /// count is far below its open volume has rooms nothing can enter.
    [[nodiscard]] usize reachable_cells() const { return reachable_count_; }

private:
    [[nodiscard]] i32 index_of(ivec3 cell) const;

    [[nodiscard]] ivec3 cell_of(vec3 world_position) const;

    ivec3 cells_{0};
    ivec3 world_voxels_{0};
    /// Breadth-first distance from the goal, in cells. -1 is unreachable.
    ///
    /// One value per cell, so a cell carries a single connected group of
    /// standable positions. Where a cell contains two pockets separated by
    /// geometry — a player-placed barricade cutting a cell in half — only the
    /// larger survives, and a body in the discarded pocket is steered toward a
    /// neighbour it cannot reach from there. That degrades to local steering
    /// rather than a hang, because a pocket is at most half a cell and cannot
    /// enlarge the reachable set. The proper fix is one node per (cell, group).
    std::vector<i32> distance_;
    usize reachable_count_ = 0;
};

}  // namespace df
