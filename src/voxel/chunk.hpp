// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

#include <array>
#include <memory>
#include <optional>
#include <unordered_map>

namespace df {

/// A 32x32x32 block of voxels.
///
/// Storage has two states. A *uniform* chunk (all empty, all bulkhead) stores a
/// single Voxel and allocates nothing — and in a derelict station the
/// overwhelming majority of chunks are uniform vacuum. A chunk only
/// materialises its dense 32 KB array when something actually writes a
/// differing voxel into it. This one trick is what keeps a 200x200x60 sector
/// inside a phone memory budget.
///
/// Damage is stored sparsely for the same reason: at any moment a handful of
/// voxels are mid-destruction, not thirty thousand.
class Chunk {
public:
    static constexpr i32 kSize = 32;
    static constexpr i32 kVolume = kSize * kSize * kSize;

    Chunk() = default;

    explicit Chunk(Voxel fill) : uniform_(fill) {}

    /// Local coordinates must be in [0, kSize). Out-of-range returns Empty.
    [[nodiscard]] Voxel at(i32 x, i32 y, i32 z) const;

    void set(i32 x, i32 y, i32 z, Voxel v);

    /// Applies `amount` damage. Returns true if the voxel was destroyed.
    bool damage(i32 x, i32 y, i32 z, u8 amount);

    [[nodiscard]] u8 damage_at(i32 x, i32 y, i32 z) const;

    /// True when every voxel in the chunk is the same material.
    [[nodiscard]] bool is_uniform() const { return uniform_.has_value(); }

    [[nodiscard]] std::optional<Voxel> uniform_value() const { return uniform_; }

    /// True when the chunk contains no solid voxels at all — the mesher can
    /// skip it outright.
    [[nodiscard]] bool is_empty() const { return uniform_ && !is_solid(*uniform_); }

    /// Set when contents change; cleared by the renderer once remeshed.
    [[nodiscard]] bool dirty() const { return dirty_; }

    void mark_dirty() { dirty_ = true; }

    void clear_dirty() { dirty_ = false; }

    /// Bytes of heap this chunk currently occupies. Used by the memory HUD.
    [[nodiscard]] usize heap_bytes() const;

    static constexpr bool in_bounds(i32 x, i32 y, i32 z) {
        return x >= 0 && y >= 0 && z >= 0 && x < kSize && y < kSize && z < kSize;
    }

    static constexpr i32 index_of(i32 x, i32 y, i32 z) {
        // x-major so that a run along x — the direction the greedy mesher
        // sweeps most often — is contiguous in memory.
        return x + kSize * (y + kSize * z);
    }

private:
    void materialise();

    std::optional<Voxel> uniform_ = Voxel::Empty;
    std::unique_ptr<std::array<Voxel, kVolume>> dense_;
    std::unordered_map<u16, u8> damage_;
    bool dirty_ = true;
};

}  // namespace df
