// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "game/wave_director.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace df {

f32 enemy_cost(EnemyKind kind) {
    switch (kind) {
        case EnemyKind::Skitter:
            return 1.0f;
        case EnemyKind::Lancer:
            return 3.5f;
        case EnemyKind::Bulwark:
            return 8.0f;
        case EnemyKind::Breacher:
            return 5.0f;
        case EnemyKind::Warden:
            return 40.0f;
        case EnemyKind::Count:
            break;
    }
    return 1.0f;
}

i32 WaveSpec::count_of(EnemyKind kind) const {
    return static_cast<i32>(std::count(roster.begin(), roster.end(), kind));
}

f32 WaveDirector::budget_for(i32 wave_index) {
    const f32 w = static_cast<f32>(std::max(1, wave_index));
    // Geometric growth with a linear floor. Pure exponential feels fine for
    // eight waves and then becomes a wall; the linear term keeps early waves
    // from being trivially thin.
    return 10.0f * w + 8.0f * std::pow(1.16f, w - 1.0f);
}

f32 WaveDirector::prep_time_for(i32 wave_index) {
    if (wave_index <= 1) return 75.0f;  // first wave is generous: you are learning to build
    const f32 t = 60.0f - 3.5f * static_cast<f32>(wave_index - 1);
    return std::max(22.0f, t);  // never so short that fortifying is impossible
}

WaveSpec WaveDirector::spec_for(i32 wave_index) const {
    WaveSpec spec;
    spec.index = std::max(1, wave_index);
    spec.budget = budget_for(spec.index);
    spec.prep_seconds = prep_time_for(spec.index);
    spec.boss_wave = spec.index % kBossEvery == 0;

    // Seeded per wave, not per run, so that inspecting wave 7 in a tool does
    // not require simulating waves 1-6.
    std::mt19937 rng(seed_ ^ (0x9E3779B9u * static_cast<u32>(spec.index)));

    f32 remaining = spec.budget;

    if (spec.boss_wave) {
        spec.roster.push_back(EnemyKind::Warden);
        remaining -= enemy_cost(EnemyKind::Warden);
        // A boss alone is a duel, not a horde. Keep a real escort.
        remaining = std::max(remaining, spec.budget * 0.35f);
    }

    // Unlock schedule. Introducing one archetype at a time is the difference
    // between a player learning the roster and a player being buried by it.
    std::vector<EnemyKind> pool{EnemyKind::Skitter};
    if (spec.index >= 3) pool.push_back(EnemyKind::Lancer);
    if (spec.index >= 5) pool.push_back(EnemyKind::Breacher);
    if (spec.index >= 7) pool.push_back(EnemyKind::Bulwark);

    const auto roster_full = [&spec] { return spec.roster.size() >= WaveDirector::kMaxRosterSize; };

    // Always seed a chunk of the budget into Skitters: the swarm is the
    // texture of a horde wave, and the expensive units read as threats only
    // against that background.
    f32 skitter_share = remaining * (spec.index < 4 ? 1.0f : 0.45f);
    remaining -= skitter_share;
    while (skitter_share >= enemy_cost(EnemyKind::Skitter) && !roster_full()) {
        spec.roster.push_back(EnemyKind::Skitter);
        skitter_share -= enemy_cost(EnemyKind::Skitter);
    }
    remaining += skitter_share;  // anything the cap refused goes back in the pot

    std::uniform_int_distribution<usize> pick(0, pool.size() - 1);
    int guard = 0;
    while (remaining >= 1.0f && !roster_full() && guard++ < 4096) {
        const EnemyKind kind = pool[pick(rng)];
        const f32 cost = enemy_cost(kind);
        if (cost > remaining) {
            // Cannot afford the pick; spend the remainder on the cheap unit
            // rather than looping forever hunting for a fit.
            if (remaining >= enemy_cost(EnemyKind::Skitter)) {
                spec.roster.push_back(EnemyKind::Skitter);
                remaining -= enemy_cost(EnemyKind::Skitter);
                continue;
            }
            break;
        }
        spec.roster.push_back(kind);
        remaining -= cost;
    }

    // Budget the roster *cap* refused becomes power instead of population.
    // Ordinary leftover change — a wave whose budget does not divide evenly
    // into enemy costs — is just dropped; scaling every enemy up by 2% for it
    // would be noise, not difficulty.
    const f32 spent = spec.budget - remaining;
    if (roster_full() && remaining > 0.5f && spent > 0.0f) {
        spec.elite_multiplier = 1.0f + remaining / spent;
    }

    // Spawn order: shuffle the tail so waves do not arrive in tidy blocks,
    // but keep any boss first so it is on the field for the whole fight.
    const auto tail = spec.roster.begin() + (spec.boss_wave ? 1 : 0);
    std::shuffle(tail, spec.roster.end(), rng);

    return spec;
}

}  // namespace df
