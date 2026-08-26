// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>

namespace df {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using ivec3 = glm::ivec3;

/// A voxel material id. 0 is always empty space; the rest index into the
/// material table (hull plating, bulkhead, ore, player-placed barricade, ...).
enum class Voxel : u8 {
    Empty = 0,
    HullPlate = 1,
    Bulkhead = 2,
    Ore = 3,
    Ice = 4,
    Barricade = 5,   // player-placed, cheap, breaks fast
    Reinforced = 6,  // player-placed, expensive, holds
};

constexpr bool is_solid(Voxel v) noexcept {
    return v != Voxel::Empty;
}

/// How many hits a voxel takes before it is removed. Drives both destruction
/// pacing and the mining loop, so it lives next to the material list.
constexpr u8 voxel_toughness(Voxel v) noexcept {
    switch (v) {
        case Voxel::Empty:
            return 0;
        case Voxel::HullPlate:
            return 3;
        case Voxel::Bulkhead:
            return 12;
        case Voxel::Ore:
            return 5;
        case Voxel::Ice:
            return 1;
        case Voxel::Barricade:
            return 6;
        case Voxel::Reinforced:
            return 20;
    }
    return 0;
}

}  // namespace df
