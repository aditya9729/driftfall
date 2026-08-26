// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "game/sim.hpp"

#include "core/log.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <vector>

namespace df {

namespace {

/// Seconds between individual spawns inside a wave. Enemies trickling in beats
/// a single dump: a horde should feel like pressure that builds, not a wall
/// that appears.
constexpr f32 kSpawnInterval = 0.45f;

constexpr i32 kTurretCost = 60;

}  // namespace

i32 build_cost(Voxel material) {
    switch (material) {
        case Voxel::Barricade:
            return 2;
        case Voxel::Reinforced:
            return 9;
        default:
            return 0;  // not buildable
    }
}

i32 salvage_yield(Voxel material) {
    switch (material) {
        case Voxel::Ore:
            return 4;
        case Voxel::Ice:
            return 1;
        case Voxel::HullPlate:
            return 1;
        case Voxel::Bulkhead:
            return 2;
        // Tearing down your own fortifications refunds nothing. Otherwise
        // build-and-refund becomes a free action and the economy stops meaning
        // anything.
        default:
            return 0;
    }
}

Sim::Sim(SimConfig config)
    : config_(config),
      world_(std::make_unique<VoxelWorld>(
          config.sector_chunks_x, config.sector_chunks_y, config.sector_chunks_z)),
      director_(config.seed),
      weapon_(WeaponStats::salvage_rifle()),
      salvage_(config.starting_salvage) {
    generate_test_sector(*world_, config.seed);
    current_wave_ = director_.spec_for(wave_index_);
    enter_phase(Phase::Prep);
}

void Sim::enter_phase(Phase next) {
    phase_ = next;
    switch (next) {
        case Phase::Prep:
            current_wave_ = director_.spec_for(wave_index_);
            phase_timer_ = current_wave_.prep_seconds;
            spawn_cursor_ = 0;
            spawn_timer_ = 0.0f;
            log::info("wave {} incoming: {} hostiles, {:.0f}s to prepare",
                      wave_index_,
                      current_wave_.size(),
                      phase_timer_);
            break;
        case Phase::Assault:
            phase_timer_ = 0.0f;
            log::info("wave {} assault begins", wave_index_);
            break;
        case Phase::Cleared:
            phase_timer_ = config_.cleared_seconds;
            log::info("wave {} cleared", wave_index_);
            break;
        case Phase::Defeat:
            log::info("run over at wave {}", wave_index_);
            break;
    }
}

i32 Sim::enemies_remaining() const {
    i32 alive = 0;
    for (const auto entity : registry_.view<const Enemy, const Health>()) {
        if (registry_.get<const Health>(entity).alive()) ++alive;
    }
    // Enemies not yet spawned still count against clearing the wave.
    return alive + static_cast<i32>(current_wave_.size() - spawn_cursor_);
}

i32 Sim::shoot_voxel(ivec3 target, u8 amount) {
    const Voxel material = world_->at(target);
    if (!is_solid(material)) return 0;
    if (!world_->damage(target, amount)) return 0;

    const i32 gained = salvage_yield(material);
    salvage_ += gained;
    return gained;
}

bool Sim::place_voxel(ivec3 target, Voxel material) {
    // Building is a Prep-phase verb. Letting the player wall themselves in
    // mid-wave removes every interesting decision from the Prep phase.
    if (phase_ != Phase::Prep) return false;

    const i32 cost = build_cost(material);
    if (cost == 0) return false;
    if (salvage_ < cost) return false;
    if (!world_->contains(target)) return false;
    if (is_solid(world_->at(target))) return false;

    world_->set(target, material);
    salvage_ -= cost;
    return true;
}

std::optional<entt::entity> Sim::deploy_turret(vec3 position) {
    if (phase_ != Phase::Prep) return std::nullopt;
    if (salvage_ < kTurretCost) return std::nullopt;

    salvage_ -= kTurretCost;
    const entt::entity turret = registry_.create();
    registry_.emplace<Transform>(turret, position, 0.0f);
    registry_.emplace<TurretBot>(turret);
    registry_.emplace<Health>(turret, 80.0f, 80.0f);
    return turret;
}

void Sim::start_wave_now() {
    if (phase_ != Phase::Prep) return;
    // Skipping prep converts unused time into salvage, so being ready early is
    // rewarded rather than merely faster.
    salvage_ += static_cast<i32>(phase_timer_ * 0.5f);
    enter_phase(Phase::Assault);
}

void Sim::spawn_pending_enemies(f32 dt) {
    if (spawn_cursor_ >= current_wave_.size()) return;

    spawn_timer_ -= dt;
    if (spawn_timer_ > 0.0f) return;
    spawn_timer_ = kSpawnInterval;

    const EnemyKind kind = current_wave_.roster[spawn_cursor_++];
    const entt::entity enemy = registry_.create();

    const f32 base_hp = [kind] {
        switch (kind) {
            case EnemyKind::Skitter:
                return 30.0f;
            case EnemyKind::Lancer:
                return 65.0f;
            case EnemyKind::Bulwark:
                return 240.0f;
            case EnemyKind::Breacher:
                return 45.0f;
            case EnemyKind::Warden:
                return 1800.0f;
            case EnemyKind::Count:
                break;
        }
        return 30.0f;
    }();

    // Late waves buy elites rather than more bodies; see WaveSpec.
    const f32 hp = base_hp * current_wave_.elite_multiplier;

    // TODO(M3): spawn at breach points on the sector hull rather than the
    // origin, so the direction an assault arrives from is itself information.
    registry_.emplace<Transform>(enemy, vec3{0.0f}, 0.0f);
    registry_.emplace<Velocity>(enemy);
    registry_.emplace<Health>(enemy, hp, hp);
    registry_.emplace<Enemy>(enemy, kind);
}

void Sim::tick_turrets(f32 dt) {
    auto turrets = registry_.view<TurretBot, const Transform>();
    auto enemies = registry_.view<const Enemy, Health, const Transform>();

    for (const auto turret_entity : turrets) {
        auto& turret = turrets.get<TurretBot>(turret_entity);
        turret.cooldown = std::max(0.0f, turret.cooldown - dt);
        if (turret.cooldown > 0.0f) continue;

        const vec3 turret_pos = turrets.get<const Transform>(turret_entity).position;

        // Nearest living target inside range. Naive O(turrets * enemies) —
        // correct and fast enough at the wave sizes we ship; revisit with a
        // spatial hash only if the profiler says so.
        entt::entity best = entt::null;
        f32 best_distance_sq = turret.range * turret.range;
        for (const auto enemy_entity : enemies) {
            auto& health = enemies.get<Health>(enemy_entity);
            if (!health.alive()) continue;
            const vec3 delta = enemies.get<const Transform>(enemy_entity).position - turret_pos;
            const f32 distance_sq = glm::dot(delta, delta);
            if (distance_sq < best_distance_sq) {
                best_distance_sq = distance_sq;
                best = enemy_entity;
            }
        }

        if (best == entt::null) continue;
        registry_.get<Health>(best).current -= turret.damage;
        turret.cooldown = turret.fire_interval;
    }
}

void Sim::tick(f32 dt) {
    weapon_.tick(dt);

    switch (phase_) {
        case Phase::Prep:
            phase_timer_ -= dt;
            if (phase_timer_ <= 0.0f) enter_phase(Phase::Assault);
            break;

        case Phase::Assault:
            phase_timer_ += dt;
            spawn_pending_enemies(dt);
            tick_turrets(dt);

            // Reap the dead before asking whether the wave is over, so the
            // wave ends on the same tick the last enemy dies. Collect first:
            // destroying entities while iterating a multi-component view
            // invalidates it.
            {
                std::vector<entt::entity> dead;
                for (const auto entity : registry_.view<const Enemy, const Health>()) {
                    if (!registry_.get<const Health>(entity).alive()) dead.push_back(entity);
                }
                for (const auto entity : dead) registry_.destroy(entity);
            }

            if (!player_health_.alive()) {
                enter_phase(Phase::Defeat);
            } else if (enemies_remaining() == 0) {
                enter_phase(Phase::Cleared);
            }
            break;

        case Phase::Cleared:
            phase_timer_ -= dt;
            if (phase_timer_ <= 0.0f) {
                ++wave_index_;
                enter_phase(Phase::Prep);
            }
            break;

        case Phase::Defeat:
            break;
    }
}

}  // namespace df
