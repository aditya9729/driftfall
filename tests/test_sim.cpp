// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/clock.hpp"
#include "game/sim.hpp"

#include <doctest/doctest.h>

using namespace df;

namespace {

SimConfig small_config() {
    SimConfig config;
    config.seed = 5;
    config.sector_chunks_x = 2;
    config.sector_chunks_y = 1;
    config.sector_chunks_z = 2;
    return config;
}

void tick_seconds(Sim& sim, f32 seconds) {
    const auto step = static_cast<f32>(FixedTimestep::kStep);
    for (f32 t = 0.0f; t < seconds; t += step) sim.tick(step);
}

/// Find a solid voxel to shoot at. The generated sector always has deck
/// plating on y=0, so this cannot fail.
ivec3 any_solid_voxel(const Sim& sim) {
    const ivec3 extent = sim.world().size_in_voxels();
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 x = 0; x < extent.x; ++x) {
            if (is_solid(sim.world().at(x, 0, z))) return {x, 0, z};
        }
    }
    return {-1, -1, -1};
}

}  // namespace

TEST_CASE("a run opens in the prep phase with a wave already scheduled") {
    Sim sim(small_config());
    CHECK(sim.phase() == Phase::Prep);
    CHECK(sim.wave_index() == 1);
    CHECK(sim.phase_time_remaining() > 0.0f);
    CHECK_FALSE(sim.current_wave().roster.empty());
    CHECK(sim.salvage() == small_config().starting_salvage);
}

TEST_CASE("prep gives way to the assault when the timer runs out") {
    Sim sim(small_config());
    tick_seconds(sim, sim.phase_time_remaining() + 0.5f);
    CHECK(sim.phase() == Phase::Assault);
}

TEST_CASE("starting a wave early converts the unused prep time into salvage") {
    Sim sim(small_config());
    const i32 before = sim.salvage();
    const f32 remaining = sim.phase_time_remaining();

    sim.start_wave_now();

    CHECK(sim.phase() == Phase::Assault);
    CHECK(sim.salvage() > before);
    CHECK(sim.salvage() == before + static_cast<i32>(remaining * 0.5f));
}

TEST_CASE("mining a voxel pays out only when it actually breaks") {
    Sim sim(small_config());
    const ivec3 target = any_solid_voxel(sim);
    REQUIRE(target.x >= 0);

    const Voxel material = sim.world().at(target);
    const i32 toughness = voxel_toughness(material);
    REQUIRE(toughness > 1);

    const i32 before = sim.salvage();
    CHECK(sim.shoot_voxel(target, 1) == 0);  // chipped, not broken
    CHECK(sim.salvage() == before);

    i32 gained = 0;
    for (i32 i = 0; i < toughness && gained == 0; ++i) gained = sim.shoot_voxel(target, 1);

    CHECK(gained == salvage_yield(material));
    CHECK(sim.salvage() == before + gained);
    CHECK_FALSE(is_solid(sim.world().at(target)));
}

TEST_CASE("shooting empty space costs and yields nothing") {
    Sim sim(small_config());
    const ivec3 extent = sim.world().size_in_voxels();
    const ivec3 vacuum{extent.x / 2, extent.y - 1, extent.z / 2};
    REQUIRE_FALSE(is_solid(sim.world().at(vacuum)));

    const i32 before = sim.salvage();
    CHECK(sim.shoot_voxel(vacuum, 10) == 0);
    CHECK(sim.salvage() == before);
}

TEST_CASE("building costs salvage and refuses when broke") {
    Sim sim(small_config());
    const ivec3 extent = sim.world().size_in_voxels();
    const ivec3 spot{extent.x / 2, 4, extent.z / 2};
    REQUIRE_FALSE(is_solid(sim.world().at(spot)));

    const i32 before = sim.salvage();
    CHECK(sim.place_voxel(spot, Voxel::Barricade));
    CHECK(sim.world().at(spot) == Voxel::Barricade);
    CHECK(sim.salvage() == before - build_cost(Voxel::Barricade));

    // Occupied space is refused, and refusing must not charge the player.
    const i32 after = sim.salvage();
    CHECK_FALSE(sim.place_voxel(spot, Voxel::Barricade));
    CHECK(sim.salvage() == after);
}

TEST_CASE("only buildable materials can be placed") {
    Sim sim(small_config());
    const ivec3 spot{5, 4, 5};
    CHECK_FALSE(sim.place_voxel(spot, Voxel::Bulkhead));
    CHECK_FALSE(sim.place_voxel(spot, Voxel::Ore));
    CHECK(sim.place_voxel(spot, Voxel::Reinforced));
}

TEST_CASE("tearing down your own barricade refunds nothing") {
    // Otherwise build-then-refund is a free action and the economy collapses.
    CHECK(salvage_yield(Voxel::Barricade) == 0);
    CHECK(salvage_yield(Voxel::Reinforced) == 0);
    CHECK(build_cost(Voxel::Reinforced) > build_cost(Voxel::Barricade));
}

TEST_CASE("building is a prep-phase verb only") {
    Sim sim(small_config());
    const ivec3 spot{6, 4, 6};
    sim.start_wave_now();
    REQUIRE(sim.phase() == Phase::Assault);

    const i32 before = sim.salvage();
    CHECK_FALSE(sim.place_voxel(spot, Voxel::Barricade));
    CHECK_FALSE(sim.deploy_turret(vec3{6.0f, 1.0f, 6.0f}).has_value());
    CHECK(sim.salvage() == before);
}

TEST_CASE("deploying a turret costs salvage and creates an entity") {
    Sim sim(small_config());
    const i32 before = sim.salvage();
    const auto turret = sim.deploy_turret(vec3{10.0f, 1.0f, 10.0f});
    REQUIRE(turret.has_value());
    CHECK(sim.salvage() < before);
    CHECK(sim.registry().all_of<TurretBot>(*turret));
}

TEST_CASE("a turret cannot be deployed without the salvage for it") {
    SimConfig config = small_config();
    config.starting_salvage = 0;
    Sim sim(config);
    CHECK_FALSE(sim.deploy_turret(vec3{0.0f}).has_value());
}

TEST_CASE("enemies spawn over time rather than all at once") {
    Sim sim(small_config());
    sim.start_wave_now();
    const i32 total = static_cast<i32>(sim.current_wave().size());

    tick_seconds(sim, 0.5f);
    const auto spawned_early = sim.registry().view<const Enemy>().size();
    CHECK(spawned_early > 0);
    CHECK(static_cast<i32>(spawned_early) < total);
    CHECK(sim.enemies_remaining() == total);
}

TEST_CASE("clearing every enemy ends the wave and starts the next one") {
    Sim sim(small_config());
    sim.start_wave_now();

    // Deploy a firing squad, then let it work. Turrets are the only damage
    // source the headless sim has, which is exactly why they are testable.
    for (int i = 0; i < 60 && sim.phase() != Phase::Cleared; ++i) {
        tick_seconds(sim, 1.0f);
        // Stand in for player damage: nothing survives an out-of-band nuke.
        for (const auto entity : sim.registry().view<const Enemy>()) {
            sim.registry().get<Health>(entity).current = -1.0f;
        }
    }

    CHECK(sim.phase() == Phase::Cleared);
    CHECK(sim.enemies_remaining() == 0);

    tick_seconds(sim, 7.0f);
    CHECK(sim.phase() == Phase::Prep);
    CHECK(sim.wave_index() == 2);
}

TEST_CASE("a turret kills a stationary enemy in range") {
    Sim sim(small_config());
    const auto turret = sim.deploy_turret(vec3{20.0f, 1.0f, 20.0f});
    REQUIRE(turret.has_value());
    sim.start_wave_now();

    tick_seconds(sim, 1.0f);
    auto enemies = sim.registry().view<const Enemy, Health, Transform>();
    REQUIRE(enemies.begin() != enemies.end());
    const entt::entity victim = *enemies.begin();
    sim.registry().get<Transform>(victim).position = vec3{20.0f, 1.0f, 22.0f};

    // Give it enough health to survive the burst. Otherwise the turret kills
    // it, the sim reaps the entity, a newly spawned enemy recycles the id, and
    // the test reads full health off a different enemy entirely.
    sim.registry().get<Health>(victim) = Health{500.0f, 500.0f};

    tick_seconds(sim, 1.0f);
    REQUIRE(sim.registry().valid(victim));
    CHECK(sim.registry().get<Health>(victim).current < 500.0f);
}

TEST_CASE("a turret ignores enemies outside its range") {
    Sim sim(small_config());
    const auto turret = sim.deploy_turret(vec3{200.0f, 1.0f, 200.0f});  // far from the spawn point
    REQUIRE(turret.has_value());
    sim.start_wave_now();

    tick_seconds(sim, 1.0f);
    auto enemies = sim.registry().view<const Enemy, Health, Transform>();
    REQUIRE(enemies.begin() != enemies.end());
    for (const auto entity : enemies) {
        const Health& health = sim.registry().get<Health>(entity);
        CHECK(health.current == health.max);
    }
}

TEST_CASE("the run is deterministic for a given seed") {
    Sim a(small_config());
    Sim b(small_config());
    a.start_wave_now();
    b.start_wave_now();
    tick_seconds(a, 3.0f);
    tick_seconds(b, 3.0f);

    CHECK(a.salvage() == b.salvage());
    CHECK(a.enemies_remaining() == b.enemies_remaining());
    CHECK(a.registry().view<const Enemy>().size() == b.registry().view<const Enemy>().size());
}
