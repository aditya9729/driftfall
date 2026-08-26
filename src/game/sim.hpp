// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "game/wave_director.hpp"
#include "game/weapon.hpp"
#include "voxel/voxel_world.hpp"

#include <entt/entity/registry.hpp>

#include <memory>
#include <optional>

namespace df {

// --- components -------------------------------------------------------------

struct Transform {
    vec3 position{0.0f};
    f32 yaw = 0.0f;
};

struct Velocity {
    vec3 value{0.0f};
};

struct Health {
    f32 current = 100.0f;
    f32 max = 100.0f;

    [[nodiscard]] bool alive() const { return current > 0.0f; }
};

struct Enemy {
    EnemyKind kind = EnemyKind::Skitter;
};

/// A player-deployed autonomous turret. The robotics half of the fantasy:
/// you do not aim these, you decide where they stand and what they cost you.
struct TurretBot {
    f32 range = 18.0f;
    f32 fire_interval = 0.35f;
    f32 cooldown = 0.0f;
    f32 damage = 14.0f;
};

// --- run state --------------------------------------------------------------

/// The run's phase. The whole game is the oscillation between two of these.
enum class Phase {
    Prep,     ///< Salvage voxels, spend them on fortifications and turrets.
    Assault,  ///< The wave is on the field. No building.
    Cleared,  ///< Short beat between waves: loot, breathe, see the next wave's makeup.
    Defeat,
};

struct SimConfig {
    u32 seed = 1337;
    i32 sector_chunks_x = 7;
    i32 sector_chunks_y = 2;
    i32 sector_chunks_z = 7;
    f32 cleared_seconds = 6.0f;
    i32 starting_salvage = 120;
};

/// Cost in salvage to place one voxel of a buildable material.
[[nodiscard]] i32 build_cost(Voxel material);

/// Salvage awarded for destroying one voxel of a material.
[[nodiscard]] i32 salvage_yield(Voxel material);

/// The headless simulation. Owns the world, the entities, and the run state,
/// and knows nothing about rendering or input. Everything the player can do is
/// a method here, which means the entire game is testable and, later,
/// replayable from a recorded input stream.
class Sim {
public:
    explicit Sim(SimConfig config);

    /// One fixed step. dt is always FixedTimestep::kStep.
    void tick(f32 dt);

    // --- player actions ---
    /// Damages a voxel. Returns salvage gained (non-zero only on destruction).
    i32 shoot_voxel(ivec3 target, u8 amount);

    /// Places a fortification voxel if the player can afford it and the target
    /// is empty. Returns false if refused.
    bool place_voxel(ivec3 target, Voxel material);

    /// Deploys a turret bot. Returns the entity, or nullopt if unaffordable.
    std::optional<entt::entity> deploy_turret(vec3 position);

    /// Ends the prep phase early. Rewards the player for being ready.
    void start_wave_now();

    // --- queries ---
    [[nodiscard]] Phase phase() const { return phase_; }

    [[nodiscard]] i32 wave_index() const { return wave_index_; }

    [[nodiscard]] const WaveSpec& current_wave() const { return current_wave_; }

    [[nodiscard]] f32 phase_time_remaining() const { return phase_timer_; }

    [[nodiscard]] i32 salvage() const { return salvage_; }

    [[nodiscard]] i32 enemies_remaining() const;

    [[nodiscard]] VoxelWorld& world() { return *world_; }

    [[nodiscard]] const VoxelWorld& world() const { return *world_; }

    [[nodiscard]] entt::registry& registry() { return registry_; }

    [[nodiscard]] Weapon& weapon() { return weapon_; }

    [[nodiscard]] const Health& player_health() const { return player_health_; }

private:
    void enter_phase(Phase next);

    void spawn_pending_enemies(f32 dt);

    void tick_turrets(f32 dt);

    SimConfig config_;
    std::unique_ptr<VoxelWorld> world_;
    WaveDirector director_;
    entt::registry registry_;
    Weapon weapon_;

    Phase phase_ = Phase::Prep;
    f32 phase_timer_ = 0.0f;
    i32 wave_index_ = 1;
    WaveSpec current_wave_;
    usize spawn_cursor_ = 0;
    f32 spawn_timer_ = 0.0f;
    i32 salvage_ = 0;
    Health player_health_{};
};

}  // namespace df
