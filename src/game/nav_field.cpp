// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "game/nav_field.hpp"

#include "voxel/chunk.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>

namespace df {
namespace {

constexpr i32 kCell = NavField::kCellSize;

/// How far the feet may rise or fall in one step between two adjacent stand
/// positions, in voxels. One voxel is 0.5 m — a stair, not a vault.
///
/// The same number governs falling, which is the conservative half of the
/// choice: a one-way drop off a ledge is *not* modelled as traversable, so the
/// field never routes an agent somewhere it cannot climb back out of. The cost
/// is that it also refuses to use such a drop as a shortcut.
constexpr i32 kMaxStepVoxels = 1;

/// The six axis neighbours, in a fixed order. Fixed because the BFS tie-breaks
/// on it and the project is deterministic-in-seed: two rebuilds of the same
/// world must produce byte-identical fields, so nothing here may depend on the
/// iteration order of an unordered container (which is also why this file never
/// calls VoxelWorld::take_dirty_chunks()).
constexpr ivec3 kNeighbours[6] = {
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
};

/// The four horizontal neighbours a single step can move between.
constexpr ivec3 kHorizontalSteps[4] = {
    {1, 0, 0},
    {-1, 0, 0},
    {0, 0, 1},
    {0, 0, -1},
};

constexpr i32 floor_div(i32 a, i32 b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

constexpr i32 ceil_div(i32 a, i32 b) {
    return a <= 0 ? 0 : (a + b - 1) / b;
}

i32 floor_to_int(f32 v) {
    return static_cast<i32>(std::floor(v));
}

/// A body's extent along one axis, in whole voxels, rounded up.
///
/// The rounding is the honest half of the body model: a 2.1-voxel-wide agent
/// needs three voxel columns and is refused a two-wide gap. The dishonest half
/// is that a body of *exactly* n voxels is assumed to be able to align itself
/// to n columns — sub-voxel placement is the collision system's problem, not
/// the flow field's, and at kCellSize=4 a one-voxel alignment argument is well
/// inside the resolution of the field anyway.
i32 span_voxels(f32 half_extent) {
    const i32 n = static_cast<i32>(std::ceil(2.0f * half_extent));
    return n < 1 ? 1 : n;
}

/// A body reduced to whole voxels: footprint w x d, standing height h.
struct Body {
    i32 w = 1;
    i32 h = 1;
    i32 d = 1;
};

/// Voxel emptiness, read through a one-chunk cache.
///
/// The sweep walks voxels in short runs down a column, so consecutive reads
/// almost always land in the same chunk; caching the last one turns a hash
/// lookup per voxel into roughly one per column. Anything outside the sector
/// reads as empty, matching VoxelWorld::at() — the sector boundary is open
/// vacuum, and a body out there has nothing under its feet anyway.
class VoxelReader {
public:
    explicit VoxelReader(const VoxelWorld& world) : world_(world), extent_(world.size_in_voxels()) {}

    [[nodiscard]] bool empty_at(i32 x, i32 y, i32 z) {
        if (x < 0 || y < 0 || z < 0) return true;
        if (x >= extent_.x || y >= extent_.y || z >= extent_.z) return true;

        const ivec3 coord{x / Chunk::kSize, y / Chunk::kSize, z / Chunk::kSize};
        if (coord != cached_coord_) {
            cached_coord_ = coord;
            cached_ = world_.chunk(coord);
            // A uniform chunk answers every query in it without being read at
            // all, and in a derelict station most chunks are uniform vacuum.
            const std::optional<Voxel> uniform =
                cached_ == nullptr ? std::optional<Voxel>{Voxel::Empty} : cached_->uniform_value();
            cached_is_uniform_ = uniform.has_value();
            cached_uniform_empty_ = !cached_is_uniform_ || !is_solid(*uniform);
        }
        if (cached_is_uniform_) return cached_uniform_empty_;
        return !is_solid(cached_->at(x % Chunk::kSize, y % Chunk::kSize, z % Chunk::kSize));
    }

    [[nodiscard]] bool has_volume() const { return extent_.x > 0 && extent_.y > 0 && extent_.z > 0; }

private:
    const VoxelWorld& world_;
    ivec3 extent_;
    ivec3 cached_coord_{-1, -1, -1};
    const Chunk* cached_ = nullptr;
    bool cached_is_uniform_ = true;
    bool cached_uniform_empty_ = true;
};

// A cell holds kCell^3 = 64 foot positions, which is exactly the width of a
// u64. The packing below leans on that, so say so rather than let a future
// change to kCellSize corrupt the masks silently.
static_assert(kCell * kCell * kCell == 64, "cell stand masks are packed into a u64");

constexpr u64 stand_bit(i32 x, i32 y, i32 z) {
    return u64{1} << (x + kCell * y + kCell * kCell * z);
}

/// The positions in a cell that touch the given neighbour — the only ones that
/// can supply the near half of a step across that face.
constexpr u64 make_boundary_mask(i32 dx, i32 dy, i32 dz) {
    const i32 delta[3]{dx, dy, dz};
    i32 lo[3]{0, 0, 0};
    i32 hi[3]{kCell - 1, kCell - 1, kCell - 1};
    for (int axis = 0; axis < 3; ++axis) {
        if (delta[axis] > 0)
            lo[axis] = hi[axis];
        else if (delta[axis] < 0)
            hi[axis] = lo[axis];
    }
    u64 mask = 0;
    for (i32 z = lo[2]; z <= hi[2]; ++z) {
        for (i32 y = lo[1]; y <= hi[1]; ++y) {
            for (i32 x = lo[0]; x <= hi[0]; ++x) mask |= stand_bit(x, y, z);
        }
    }
    return mask;
}

/// Indexed by the same order as kNeighbours, and must stay that way.
constexpr u64 kBoundaryMasks[6] = {
    make_boundary_mask(1, 0, 0),
    make_boundary_mask(-1, 0, 0),
    make_boundary_mask(0, 1, 0),
    make_boundary_mask(0, -1, 0),
    make_boundary_mask(0, 0, 1),
    make_boundary_mask(0, 0, -1),
};

/// The stand positions of one cell, as a mask over its 4x4x4 foot positions,
/// computed on demand and kept.
///
/// Two things make this necessary rather than decorative.
///
/// First, a cell is one node in the sweep, which silently asserts that
/// everything inside it is mutually reachable. A 1-voxel bulkhead running
/// through the *middle* of a cell breaks that: the cell holds open floor on
/// both sides, and a field that only tested cell boundaries would connect them
/// and route the horde into the wall — the original soft-lock wearing a second
/// costume. The generated sector hides this, because its rooms are 24 voxels
/// apart and 24 divides by kCellSize, so every bulkhead lands exactly on a cell
/// face. Player-placed barricades will not be so considerate.
///
/// So each cell keeps only its largest internally-connected group of stand
/// positions and discards the rest. Largest rather than all: a cell containing
/// a two-voxel ore stack has an isolated perch on top of it that no body can
/// step onto from inside that cell, and dropping the whole cell over that would
/// shred the field. Ties go to the group holding the lowest position index, so
/// the choice is a function of the world and not of visit order.
///
/// The residual cost, named honestly: a body standing in a discarded group of a
/// reachable cell is told the cell's distance and steered toward a neighbour it
/// cannot actually reach from that pocket. The pocket is at most half a cell.
/// Fixing it properly means one node per (cell, group), which is a wider
/// distance_ than the header exposes.
class CellStands {
public:
    CellStands(VoxelReader& reader, const Body& body, usize cell_count)
        : reader_(reader),
          body_(body),
          columns_x_(kCell + body.w - 1),
          columns_z_(kCell + body.d - 1),
          levels_(kCell + body.h),
          run_(static_cast<usize>(columns_x_) * static_cast<usize>(columns_z_) * static_cast<usize>(levels_),
               0),
          mask_(cell_count, 0),
          known_(cell_count, 0) {}

    [[nodiscard]] u64 mask(i32 index, ivec3 cell) {
        const auto slot = static_cast<usize>(index);
        if (known_[slot] == 0u) {
            mask_[slot] = compute(cell);
            known_[slot] = 1u;
        }
        return mask_[slot];
    }

private:
    /// Level `l` of the scratch is world y = base_y - 1 + l. Level 0 is the
    /// voxel a body with its feet on the cell floor stands on; the top level is
    /// the highest one the tallest placement in the cell needs.
    [[nodiscard]] usize run_index(i32 column_x, i32 column_z, i32 level) const {
        return (static_cast<usize>(column_z) * static_cast<usize>(columns_x_) +
                static_cast<usize>(column_x)) *
                   static_cast<usize>(levels_) +
               static_cast<usize>(level);
    }

    /// Fills the scratch with, per column and level, how many empty voxels run
    /// upward from there — capped at the body height, because that is the most
    /// any placement asks about. Zero means solid.
    ///
    /// This is the only place voxels are read. The footprint overhang is
    /// included, so a body whose feet are in the cell but whose bulk leans into
    /// the next one is judged on the voxels it actually occupies.
    void sample_columns(ivec3 base) {
        // One column at a time, walking down it. Sweeping level-major instead
        // reads the chunk's storage more sequentially but has to carry the
        // running count for every column in a side array, and measuring says
        // that costs more than the locality wins back.
        for (i32 cz = 0; cz < columns_z_; ++cz) {
            const i32 z = base.z + cz;
            for (i32 cx = 0; cx < columns_x_; ++cx) {
                const i32 x = base.x + cx;
                i32 above = 0;
                for (i32 level = levels_ - 1; level >= 0; --level) {
                    const i32 y = base.y - 1 + level;
                    above = reader_.empty_at(x, y, z) ? std::min(body_.h, above + 1) : 0;
                    run_[run_index(cx, cz, level)] = static_cast<u8>(above);
                }
            }
        }
    }

    /// Whether a body with its feet at the cell-local position fits and has a
    /// floor under every column of its footprint.
    ///
    /// Fit alone is the mistake this guards against: a field that only checked
    /// headroom would route a Bulwark out over a pit or into a gap it cannot
    /// occupy, which is worse than having no field — the agent commits to a
    /// direction that does not exist and pins there.
    [[nodiscard]] bool standable(i32 x, i32 y, i32 z) const {
        for (i32 dz = 0; dz < body_.d; ++dz) {
            for (i32 dx = 0; dx < body_.w; ++dx) {
                // Something solid directly underfoot...
                if (run_[run_index(x + dx, z + dz, y)] != 0u) return false;
                // ...and the body's full height clear above it.
                if (static_cast<i32>(run_[run_index(x + dx, z + dz, y + 1)]) < body_.h) return false;
            }
        }
        return true;
    }

    [[nodiscard]] u64 compute(ivec3 cell) {
        sample_columns(cell * kCell);

        u64 raw = 0;
        for (i32 z = 0; z < kCell; ++z) {
            for (i32 y = 0; y < kCell; ++y) {
                for (i32 x = 0; x < kCell; ++x) {
                    if (standable(x, y, z)) raw |= stand_bit(x, y, z);
                }
            }
        }
        if (raw == 0) return 0;

        u64 remaining = raw;
        u64 best = 0;
        i32 best_size = 0;
        while (remaining != 0) {
            const i32 seed = std::countr_zero(remaining);
            const u64 group = flood(raw, seed);
            remaining &= ~group;
            const auto size = static_cast<i32>(std::popcount(group));
            // Strictly greater, so the first group found — the one holding the
            // lowest position index — wins a tie.
            if (size > best_size) {
                best_size = size;
                best = group;
            }
        }
        return best;
    }

    /// Everything reachable from `seed` without leaving the cell, using the
    /// same one-voxel step the sweep itself uses.
    [[nodiscard]] static u64 flood(u64 raw, i32 seed) {
        i32 stack[64];
        i32 top = 0;
        u64 group = u64{1} << seed;
        stack[top++] = seed;

        while (top > 0) {
            const i32 position = stack[--top];
            const i32 x = position % kCell;
            const i32 y = (position / kCell) % kCell;
            const i32 z = position / (kCell * kCell);

            for (const ivec3& step : kHorizontalSteps) {
                for (i32 dy = -kMaxStepVoxels; dy <= kMaxStepVoxels; ++dy) {
                    const i32 nx = x + step.x;
                    const i32 ny = y + dy;
                    const i32 nz = z + step.z;
                    if (nx < 0 || ny < 0 || nz < 0) continue;
                    if (nx >= kCell || ny >= kCell || nz >= kCell) continue;
                    const u64 bit = stand_bit(nx, ny, nz);
                    if ((raw & bit) == 0 || (group & bit) != 0) continue;
                    group |= bit;
                    stack[top++] = nx + kCell * ny + kCell * kCell * nz;
                }
            }
        }
        return group;
    }

    VoxelReader& reader_;
    Body body_;
    i32 columns_x_;
    i32 columns_z_;
    i32 levels_;
    std::vector<u8> run_;
    std::vector<u64> mask_;
    std::vector<u8> known_;
};

/// Whether a body can step from a stand position inside `from` to one inside
/// `to`.
///
/// Connectivity is a property of the boundary between two cells, not of the
/// cells themselves: there has to be an actual pair of adjacent stand
/// positions, one on each side, both in their cell's retained group.
///
/// `from_is_wildcard` relaxes the requirement on the `from` side only, and is
/// used for the goal cell alone — see rebuild().
bool cells_connect(CellStands& stands,
                   i32 from_index,
                   ivec3 from,
                   i32 to_index,
                   ivec3 to,
                   i32 direction,
                   bool from_is_wildcard) {
    const u64 to_mask = stands.mask(to_index, to);
    if (to_mask == 0) return false;
    const u64 from_mask = from_is_wildcard ? ~u64{0} : stands.mask(from_index, from);
    if (from_mask == 0) return false;

    // Only the layer of `from` that touches `to` can supply the near half of
    // the step, and only positions a body can actually stand in. Walking the
    // set bits of the intersection rather than all 64 positions is most of what
    // makes the sweep cheap: on an open deck a cell has stand positions on one
    // level out of four.
    u64 candidates = from_mask & kBoundaryMasks[direction];
    if (candidates == 0) return false;

    const ivec3 delta = kNeighbours[direction];
    // The offset that turns a local position in `from` into one in `to`.
    const ivec3 shift = delta * kCell;
    // A step across a horizontal face has to go that way; only a step onto a
    // cell above or below is free to pick its horizontal direction.
    const bool vertical = delta.y != 0;

    while (candidates != 0) {
        const i32 position = std::countr_zero(candidates);
        candidates &= candidates - 1;
        const i32 x = position % kCell;
        const i32 y = (position / kCell) % kCell;
        const i32 z = position / (kCell * kCell);

        for (const ivec3& step : kHorizontalSteps) {
            if (!vertical && (step.x != delta.x || step.z != delta.z)) continue;
            for (i32 dy = -kMaxStepVoxels; dy <= kMaxStepVoxels; ++dy) {
                const i32 nx = x + step.x - shift.x;
                const i32 ny = y + dy - shift.y;
                const i32 nz = z + step.z - shift.z;
                // The far half of the step has to land inside `to`. A move that
                // lands in some third cell is a diagonal one, which this
                // six-connected field does not model.
                if (nx < 0 || ny < 0 || nz < 0) continue;
                if (nx >= kCell || ny >= kCell || nz >= kCell) continue;
                if ((to_mask & stand_bit(nx, ny, nz)) != 0) return true;
            }
        }
    }
    return false;
}

bool finite_and_non_negative(vec3 v) {
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(v[axis]) || v[axis] < 0.0f) return false;
    }
    return true;
}

}  // namespace

void NavField::reset(ivec3 world_size_voxels) {
    world_voxels_ = world_size_voxels;
    for (int axis = 0; axis < 3; ++axis) {
        if (world_voxels_[axis] < 0) world_voxels_[axis] = 0;
    }

    cells_ = ivec3{ceil_div(world_voxels_.x, kCellSize),
                   ceil_div(world_voxels_.y, kCellSize),
                   ceil_div(world_voxels_.z, kCellSize)};

    // A sector with no extent on any axis has no cells at all, rather than a
    // degenerate slab of them. The upper guard is not hypothetical hygiene:
    // cell indices are i32, so a sector large enough to overflow one would
    // silently alias cells onto each other rather than fail.
    const i64 count = static_cast<i64>(cells_.x) * static_cast<i64>(cells_.y) * static_cast<i64>(cells_.z);
    if (count <= 0 || count > static_cast<i64>(std::numeric_limits<i32>::max())) {
        cells_ = ivec3{0, 0, 0};
        distance_.clear();
        reachable_count_ = 0;
        return;
    }

    distance_.assign(static_cast<usize>(count), -1);
    reachable_count_ = 0;
}

void NavField::rebuild(const VoxelWorld& world, ivec3 goal, vec3 half_extents) {
    // Sizing is the caller's job via reset(), but a field silently mismatched
    // to its world would index into the wrong cells, so re-derive it rather
    // than trust it.
    const ivec3 extent = world.size_in_voxels();
    if (extent != world_voxels_) reset(extent);

    std::fill(distance_.begin(), distance_.end(), -1);
    reachable_count_ = 0;
    if (distance_.empty()) return;

    // A body with a non-finite or negative extent is not a body. Refuse it
    // outright: guessing a size here would produce a field that looks valid
    // and routes something that does not exist.
    if (!finite_and_non_negative(half_extents)) return;

    const Body body{span_voxels(half_extents.x), span_voxels(half_extents.y), span_voxels(half_extents.z)};

    const ivec3 goal_cell{
        floor_div(goal.x, kCellSize), floor_div(goal.y, kCellSize), floor_div(goal.z, kCellSize)};
    const i32 goal_index = index_of(goal_cell);
    if (goal_index < 0) return;  // a goal outside the sector has no field to offer

    VoxelReader reader(world);
    if (!reader.has_volume()) return;

    // Stand positions are computed per cell on demand, not for the whole sector
    // up front. The sweep only ever asks about cells it reached and their
    // immediate neighbours — a few thousand of the fifty thousand in a default
    // sector — so precomputing them all would spend an order of magnitude more
    // time, nearly all of it on vacuum nothing can stand in.
    CellStands stands(reader, body, distance_.size());

    // The goal is wherever the player is, and a player mid-jump or clipped into
    // geometry occupies no stand position at all. Blanking the whole field for
    // the airborne frames of a jump would stall the horde, so the goal cell —
    // and only the goal cell — is allowed to seed the sweep without being
    // standable. The cost is that a goal cell straddling a bulkhead can leak
    // one hop through it; it is one cell, and it is transient.
    const bool goal_is_wildcard = stands.mask(goal_index, goal_cell) == 0;

    distance_[static_cast<usize>(goal_index)] = 0;

    // Breadth-first over unit-cost edges, expanded a whole ring at a time.
    // Two vectors rather than one queue: the edges all cost the same, so a
    // priority queue would buy nothing but a heap and a tie-break to argue
    // about, and the ring's index *is* the distance.
    std::vector<i32> frontier;
    std::vector<i32> next_frontier;
    frontier.push_back(goal_index);
    usize reached = 1;

    for (i32 ring = 1; !frontier.empty(); ++ring) {
        next_frontier.clear();
        for (const i32 current : frontier) {
            const i32 cx = current % cells_.x;
            const i32 cy = (current / cells_.x) % cells_.y;
            const i32 cz = current / (cells_.x * cells_.y);
            const ivec3 cell{cx, cy, cz};
            const bool wildcard = goal_is_wildcard && current == goal_index;

            for (i32 direction = 0; direction < 6; ++direction) {
                const ivec3 neighbour = cell + kNeighbours[direction];
                const i32 neighbour_index = index_of(neighbour);
                if (neighbour_index < 0) continue;
                if (distance_[static_cast<usize>(neighbour_index)] >= 0) continue;
                if (!cells_connect(stands, current, cell, neighbour_index, neighbour, direction, wildcard)) {
                    continue;
                }
                distance_[static_cast<usize>(neighbour_index)] = ring;
                next_frontier.push_back(neighbour_index);
            }
        }
        reached += next_frontier.size();
        frontier.swap(next_frontier);
    }

    reachable_count_ = reached;
}

vec3 NavField::direction_at(vec3 world_position) const {
    const i32 index = index_of(cell_of(world_position));
    if (index < 0) return vec3{0.0f};

    const i32 here = distance_[static_cast<usize>(index)];
    // -1 is unreachable; 0 is the goal cell itself, which has nowhere downhill
    // to point. Both report no direction rather than inventing one.
    if (here <= 0) return vec3{0.0f};

    const i32 cx = index % cells_.x;
    const i32 cy = (index / cells_.x) % cells_.y;
    const i32 cz = index / (cells_.x * cells_.y);
    const ivec3 cell{cx, cy, cz};

    i32 best = here;
    ivec3 best_cell{0, 0, 0};
    bool found = false;
    for (const ivec3& offset : kNeighbours) {
        const i32 neighbour_index = index_of(cell + offset);
        if (neighbour_index < 0) continue;
        const i32 neighbour_distance = distance_[static_cast<usize>(neighbour_index)];
        // Strictly downhill, and ties go to the earlier neighbour in the fixed
        // order — the field has to be a function of the world, not of the order
        // something happened to be visited in.
        if (neighbour_distance >= 0 && neighbour_distance < best) {
            best = neighbour_distance;
            best_cell = cell + offset;
            found = true;
        }
    }
    if (!found) return vec3{0.0f};

    // Aim at the downhill cell's centre rather than emitting a raw axis vector.
    // Same cell sequence, but the agent is pulled toward the middle of the
    // corridor as it goes, which is what keeps it off the door frame.
    const vec3 centre{static_cast<f32>(best_cell.x * kCellSize) + 0.5f * static_cast<f32>(kCellSize),
                      static_cast<f32>(best_cell.y * kCellSize) + 0.5f * static_cast<f32>(kCellSize),
                      static_cast<f32>(best_cell.z * kCellSize) + 0.5f * static_cast<f32>(kCellSize)};
    const vec3 delta = centre - world_position;
    const f32 length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (!std::isfinite(length) || length < 1.0e-6f) return vec3{0.0f};
    return delta / length;
}

bool NavField::reachable(vec3 world_position) const {
    return distance_at(world_position) >= 0;
}

i32 NavField::distance_at(vec3 world_position) const {
    const i32 index = index_of(cell_of(world_position));
    if (index < 0) return -1;
    return distance_[static_cast<usize>(index)];
}

i32 NavField::index_of(ivec3 cell) const {
    if (cell.x < 0 || cell.y < 0 || cell.z < 0) return -1;
    if (cell.x >= cells_.x || cell.y >= cells_.y || cell.z >= cells_.z) return -1;
    return cell.x + cells_.x * (cell.y + cells_.y * cell.z);
}

ivec3 NavField::cell_of(vec3 world_position) const {
    // Far outside any sector the float-to-int conversion is undefined, not
    // merely wrong, so reject the input before converting it. Same reflex as
    // raycast_voxels rejecting a zero or non-finite direction.
    constexpr f32 kLimit = 1.0e7f;
    for (int axis = 0; axis < 3; ++axis) {
        const f32 v = world_position[axis];
        if (!std::isfinite(v) || v < -kLimit || v > kLimit) return ivec3{-1, -1, -1};
    }
    return ivec3{floor_div(floor_to_int(world_position.x), kCellSize),
                 floor_div(floor_to_int(world_position.y), kCellSize),
                 floor_div(floor_to_int(world_position.z), kCellSize)};
}

}  // namespace df
