// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

namespace df {

/// Outcome of tapping the reload button during a reload.
enum class ReloadResult {
    Ignored,  ///< Not reloading; the tap did nothing.
    Perfect,  ///< Hit the narrow window: fast reload plus a damage-boosted magazine.
    Good,     ///< Hit the wide window: fast reload, no bonus.
    Jammed,   ///< Missed. The weapon locks out for a punishing extra beat.
};

enum class ReloadState { Ready, Reloading, Jammed };

/// Tuning for one weapon.
///
/// The active-reload window numbers are the single most feel-critical values in
/// the game. They are data, not code, so they can be tuned from a config file
/// without a rebuild once the balance pass starts.
struct WeaponStats {
    f32 damage = 24.0f;
    /// Damage dealt to voxels per shot. Decoupled from enemy damage so that
    /// "shreds enemies, barely scratches walls" is expressible.
    u8 voxel_damage = 1;
    i32 mag_size = 30;
    f32 fire_interval = 0.085f;  ///< seconds between shots
    f32 reload_seconds = 2.10f;  ///< full reload if you never tap
    f32 perfect_start = 0.62f;   ///< window openings, as a fraction of reload_seconds
    f32 perfect_end = 0.70f;
    f32 good_end = 0.86f;
    f32 jam_penalty = 1.15f;  ///< extra seconds added when you miss the window
    f32 perfect_damage_bonus = 1.30f;

    static WeaponStats salvage_rifle();
    static WeaponStats breach_shotgun();
    static WeaponStats mining_lance();
};

/// A weapon's runtime state. Pure logic, no rendering, fully unit-testable —
/// gunfeel is too important to only be verifiable by playing.
class Weapon {
public:
    explicit Weapon(WeaponStats stats);

    void tick(f32 dt);

    /// Attempts to fire. Returns the damage dealt, or 0 if it could not fire
    /// (empty, mid-reload, still on the fire-rate cooldown).
    f32 fire();

    void begin_reload();

    /// The active-reload tap. Timing is judged against the windows in stats.
    ReloadResult tap_reload();

    [[nodiscard]] ReloadState state() const { return state_; }

    [[nodiscard]] i32 ammo() const { return ammo_; }

    [[nodiscard]] const WeaponStats& stats() const { return stats_; }

    /// Progress through the current reload in [0,1]. The UI draws the window
    /// bar from this, so it must be exactly what tap_reload() judges against.
    [[nodiscard]] f32 reload_progress() const;

    /// True while the current magazine carries the perfect-reload bonus.
    [[nodiscard]] bool boosted() const { return boosted_; }

private:
    WeaponStats stats_;
    ReloadState state_ = ReloadState::Ready;
    i32 ammo_ = 0;
    f32 reload_elapsed_ = 0.0f;
    f32 reload_target_ = 0.0f;
    f32 fire_cooldown_ = 0.0f;
    bool boosted_ = false;
    bool tapped_ = false;
};

}  // namespace df
