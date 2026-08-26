// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "voxel/chunk.hpp"

namespace df {

Voxel Chunk::at(i32 x, i32 y, i32 z) const {
    if (!in_bounds(x, y, z)) return Voxel::Empty;
    if (uniform_) return *uniform_;
    return (*dense_)[static_cast<usize>(index_of(x, y, z))];
}

void Chunk::materialise() {
    if (!uniform_) return;
    dense_ = std::make_unique<std::array<Voxel, kVolume>>();
    dense_->fill(*uniform_);
    uniform_.reset();
}

void Chunk::set(i32 x, i32 y, i32 z, Voxel v) {
    if (!in_bounds(x, y, z)) return;

    if (uniform_) {
        if (*uniform_ == v) return;  // no-op write, stay compact
        materialise();
    }

    const auto idx = static_cast<usize>(index_of(x, y, z));
    if ((*dense_)[idx] == v) return;

    (*dense_)[idx] = v;
    damage_.erase(static_cast<u16>(idx));
    dirty_ = true;
}

u8 Chunk::damage_at(i32 x, i32 y, i32 z) const {
    if (!in_bounds(x, y, z)) return 0;
    const auto it = damage_.find(static_cast<u16>(index_of(x, y, z)));
    return it == damage_.end() ? u8{0} : it->second;
}

bool Chunk::damage(i32 x, i32 y, i32 z, u8 amount) {
    if (!in_bounds(x, y, z) || amount == 0) return false;

    const Voxel v = at(x, y, z);
    if (!is_solid(v)) return false;

    const u8 toughness = voxel_toughness(v);
    const auto key = static_cast<u16>(index_of(x, y, z));
    const u32 accumulated = static_cast<u32>(damage_at(x, y, z)) + amount;

    if (accumulated >= toughness) {
        set(x, y, z, Voxel::Empty);
        return true;
    }

    damage_[key] = static_cast<u8>(accumulated);
    // Damage changes the voxel's appearance (cracks, glow), so the chunk still
    // needs a remesh even though no voxel was removed.
    dirty_ = true;
    return false;
}

usize Chunk::heap_bytes() const {
    usize bytes = 0;
    if (dense_) bytes += sizeof(*dense_);
    bytes += damage_.size() * (sizeof(u16) + sizeof(u8) + 16);  // rough node overhead
    return bytes;
}

}  // namespace df
