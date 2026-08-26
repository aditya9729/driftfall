// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "game/weapon.hpp"

#include <algorithm>

namespace df {

WeaponStats WeaponStats::salvage_rifle() {
    return WeaponStats{};  // the defaults are the rifle; everything else deviates from it
}

WeaponStats WeaponStats::breach_shotgun() {
    WeaponStats s;
    s.damage = 96.0f;
    s.voxel_damage = 4;  // the wall-opener: this is the tool for making new sightlines
    s.mag_size = 6;
    s.fire_interval = 0.72f;
    s.reload_seconds = 2.60f;
    s.perfect_start = 0.58f;
    s.perfect_end = 0.65f;
    s.good_end = 0.82f;
    s.jam_penalty = 1.40f;
    return s;
}

WeaponStats WeaponStats::mining_lance() {
    WeaponStats s;
    s.damage = 9.0f;     // poor against armour...
    s.voxel_damage = 6;  // ...but this is how you harvest a sector fast
    s.mag_size = 100;
    s.fire_interval = 0.05f;
    s.reload_seconds = 1.40f;
    s.perfect_start = 0.55f;
    s.perfect_end = 0.68f;
    s.good_end = 0.90f;
    s.jam_penalty = 0.70f;
    s.perfect_damage_bonus = 1.0f;
    return s;
}

Weapon::Weapon(WeaponStats stats) : stats_(stats), ammo_(stats.mag_size) {}

f32 Weapon::reload_progress() const {
    if (state_ == ReloadState::Ready || reload_target_ <= 0.0f) return 0.0f;
    return std::clamp(reload_elapsed_ / reload_target_, 0.0f, 1.0f);
}

void Weapon::tick(f32 dt) {
    fire_cooldown_ = std::max(0.0f, fire_cooldown_ - dt);

    if (state_ == ReloadState::Ready) return;

    reload_elapsed_ += dt;
    if (reload_elapsed_ >= reload_target_) {
        ammo_ = stats_.mag_size;
        state_ = ReloadState::Ready;
        reload_elapsed_ = 0.0f;
        reload_target_ = 0.0f;
        tapped_ = false;
    }
}

f32 Weapon::fire() {
    if (state_ != ReloadState::Ready) return 0.0f;
    if (ammo_ <= 0) return 0.0f;
    if (fire_cooldown_ > 0.0f) return 0.0f;

    --ammo_;
    fire_cooldown_ = stats_.fire_interval;

    f32 damage = stats_.damage;
    if (boosted_) damage *= stats_.perfect_damage_bonus;

    // Running the magazine dry is what forces the reload gamble. Auto-reload is
    // deliberately absent: the player chooses when to be vulnerable.
    if (ammo_ == 0) boosted_ = false;

    return damage;
}

void Weapon::begin_reload() {
    if (state_ != ReloadState::Ready) return;
    if (ammo_ == stats_.mag_size) return;

    state_ = ReloadState::Reloading;
    reload_elapsed_ = 0.0f;
    reload_target_ = stats_.reload_seconds;
    boosted_ = false;
    tapped_ = false;
}

ReloadResult Weapon::tap_reload() {
    if (state_ != ReloadState::Reloading) return ReloadResult::Ignored;

    // One judged tap per reload. Without this, mashing the button guarantees a
    // perfect every time and the whole risk/reward of the mechanic evaporates.
    if (tapped_) return ReloadResult::Ignored;
    tapped_ = true;

    const f32 t = reload_progress();

    if (t >= stats_.perfect_start && t <= stats_.perfect_end) {
        ammo_ = stats_.mag_size;
        state_ = ReloadState::Ready;
        reload_elapsed_ = 0.0f;
        reload_target_ = 0.0f;
        boosted_ = true;
        return ReloadResult::Perfect;
    }

    if (t > stats_.perfect_end && t <= stats_.good_end) {
        ammo_ = stats_.mag_size;
        state_ = ReloadState::Ready;
        reload_elapsed_ = 0.0f;
        reload_target_ = 0.0f;
        return ReloadResult::Good;
    }

    // Missed. The reload does not restart — it stretches. Punishing, but never
    // unrecoverable, and the extra time is visible on the same bar.
    reload_target_ = stats_.reload_seconds + stats_.jam_penalty;
    return ReloadResult::Jammed;
}

}  // namespace df
