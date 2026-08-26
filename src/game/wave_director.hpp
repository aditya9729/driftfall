// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

#include <vector>

namespace df {

/// The derelict's defence swarm. Every enemy is a machine — no human bodies
/// anywhere in DRIFTFALL. That is a deliberate design and business decision:
/// it keeps the age rating low, widens the audience, costs no gore technology,
/// and is more on-theme than the alternative.
enum class EnemyKind : u8 {
    Skitter,   ///< Fast, fragile, arrives in numbers. Teaches you to hold a line.
    Lancer,    ///< Ranged. Punishes standing still; forces you to use cover.
    Bulwark,   ///< Armoured front plate. Must be flanked or breached.
    Breacher,  ///< Detonates on contact and destroys voxels — including yours.
    Warden,    ///< Boss. Rebuilds the station's walls against you.
    Count,
};

/// Spawn cost of one enemy, in wave-budget points.
[[nodiscard]] f32 enemy_cost(EnemyKind kind);

/// The wave the director decided to run.
struct WaveSpec {
    i32 index = 1;            ///< 1-based
    f32 budget = 0.0f;        ///< points spent on this wave
    f32 prep_seconds = 0.0f;  ///< salvage/build time granted before it starts
    bool boss_wave = false;
    std::vector<EnemyKind> roster;  ///< the actual enemies, in spawn order

    /// Health and damage scale applied to every enemy in this wave.
    ///
    /// Past a certain point the budget cannot buy more bodies — a phone cannot
    /// draw or path four hundred of them, and a player cannot read that fight
    /// anyway. So beyond the roster cap the budget buys *elites* instead:
    /// the same silhouettes, tougher. Late waves get harder without the frame
    /// rate falling apart.
    f32 elite_multiplier = 1.0f;

    [[nodiscard]] i32 count_of(EnemyKind kind) const;

    [[nodiscard]] usize size() const { return roster.size(); }
};

/// Decides what each wave contains.
///
/// Budget-driven rather than hand-authored, so the difficulty curve is one
/// readable formula instead of fifty tuning tables — and so an endless mode
/// works for free. Fully deterministic in the run seed: same seed, same run,
/// which is what makes daily challenges and replays possible later.
class WaveDirector {
public:
    explicit WaveDirector(u32 seed) : seed_(seed) {}

    [[nodiscard]] WaveSpec spec_for(i32 wave_index) const;

    /// Points available at a given wave. Exposed for balance tests, which
    /// assert the curve stays monotonic and does not spike at boss waves.
    [[nodiscard]] static f32 budget_for(i32 wave_index);

    /// Salvage/build time before a wave. Shrinks as the run goes on: early
    /// waves are about building, late waves are about whether you built well.
    [[nodiscard]] static f32 prep_time_for(i32 wave_index);

    static constexpr i32 kBossEvery = 5;

    /// Hard ceiling on concurrent bodies in a wave. Derived from the frame
    /// budget, not from taste: ~220 skinned enemies is what an iPhone 12 can
    /// animate, path and draw inside 16.6 ms alongside the voxel world.
    static constexpr usize kMaxRosterSize = 220;

private:
    u32 seed_;
};

}  // namespace df
