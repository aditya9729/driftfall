// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// These are adversarial by intent. The happy path — an enemy in an open room
// walks at the player — is the one case that was never in doubt. What is in
// doubt is the wall with a doorway in it, the tick rate, and the sign of a dot
// product, so that is what is asserted here.

#include "core/clock.hpp"
#include "game/enemy_ai.hpp"
#include "game/nav_field.hpp"
#include "game/sim.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <cmath>

using namespace df;

namespace {

constexpr f32 kStep = static_cast<f32>(FixedTimestep::kStep);

/// Two rooms, 64 x 32 x 64 voxels, split by a full-height partition at x = 32
/// with one 3x3 doorway punched at z = 31..33. This is generate_test_sector's
/// geometry reduced to the single feature that matters: an enemy cannot reach
/// the player by pointing at them.
VoxelWorld two_rooms() {
    VoxelWorld world(2, 1, 2);
    const ivec3 extent = world.size_in_voxels();

    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 x = 0; x < extent.x; ++x) world.set(x, 0, z, Voxel::Bulkhead);
    }
    for (i32 y = 1; y <= 7; ++y) {
        for (i32 x = 0; x < extent.x; ++x) {
            world.set(x, y, 0, Voxel::HullPlate);
            world.set(x, y, extent.z - 1, Voxel::HullPlate);
        }
        for (i32 z = 0; z < extent.z; ++z) {
            world.set(0, y, z, Voxel::HullPlate);
            world.set(extent.x - 1, y, z, Voxel::HullPlate);
            world.set(32, y, z, Voxel::HullPlate);  // the partition
        }
    }
    for (i32 y = 1; y <= 3; ++y) {
        for (i32 z = 31; z <= 33; ++z) world.set(32, y, z, Voxel::Empty);
    }
    return world;
}

/// One room, no partition. For the tests that are not about navigation.
VoxelWorld open_room() {
    VoxelWorld world(2, 1, 2);
    const ivec3 extent = world.size_in_voxels();
    for (i32 z = 0; z < extent.z; ++z) {
        for (i32 x = 0; x < extent.x; ++x) world.set(x, 0, z, Voxel::Bulkhead);
    }
    for (i32 y = 1; y <= 7; ++y) {
        for (i32 x = 0; x < extent.x; ++x) {
            world.set(x, y, 0, Voxel::HullPlate);
            world.set(x, y, extent.z - 1, Voxel::HullPlate);
        }
        for (i32 z = 0; z < extent.z; ++z) {
            world.set(0, y, z, Voxel::HullPlate);
            world.set(extent.x - 1, y, z, Voxel::HullPlate);
        }
    }
    return world;
}

void rebuild_for(NavField& nav, const VoxelWorld& world, vec3 player) {
    nav.reset(world.size_in_voxels());
    nav.rebuild(world,
                ivec3{static_cast<i32>(std::floor(player.x)),
                      static_cast<i32>(std::floor(player.y)),
                      static_cast<i32>(std::floor(player.z))},
                nav_body_half_extents());
}

entt::entity spawn(entt::registry& registry, EnemyKind kind, vec3 position, f32 hp = 500.0f) {
    const entt::entity enemy = registry.create();
    registry.emplace<Transform>(enemy, position, 0.0f);
    registry.emplace<Velocity>(enemy);
    registry.emplace<Health>(enemy, hp, hp);
    registry.emplace<Enemy>(enemy, kind);
    return enemy;
}

/// Feet on the deck (top face at y = 1) for a body of this kind.
vec3 standing_at(EnemyKind kind, f32 x, f32 z) {
    return {x, 1.0f + tuning_for(kind).half_extents.y + 0.01f, z};
}

}  // namespace

// --- the soft-lock ----------------------------------------------------------

TEST_CASE("an enemy walled off from the player still reaches them through the doorway") {
    // The regression this whole module is shaped around. Pointing at the
    // player pins the enemy on the partition at x = 32 forever; the wave never
    // clears and the Assault phase hangs, headlessly and deterministically.
    VoxelWorld world = two_rooms();
    const vec3 player = standing_at(EnemyKind::Skitter, 48.0f, 32.5f);

    NavField nav;
    rebuild_for(nav, world, player);
    REQUIRE(nav.reachable(standing_at(EnemyKind::Skitter, 8.0f, 8.0f)));

    entt::registry registry;
    const entt::entity skitter =
        spawn(registry, EnemyKind::Skitter, standing_at(EnemyKind::Skitter, 8.0f, 8.0f));

    f32 closest = 1.0e9f;
    for (i32 i = 0; i < 60 * 20; ++i) {
        const EnemyTickResult step = tick_enemies(registry, world, nav, player, kStep);
        const vec3 position = registry.get<Transform>(skitter).position;
        closest = std::min(closest, glm::length(player - position));
        if (step.attacks_landed > 0) break;
    }

    CHECK(static_cast<f64>(closest) < 2.0);
    // And it did not simply clip through: it had to be on the far side of the
    // partition, which is only possible through the doorway.
    CHECK(registry.get<Transform>(skitter).position.x > 32.0f);
}

TEST_CASE("the real generated sector is crossable from the spawn point") {
    // The shipping case, not a fixture: a default-sized sector, enemies at the
    // origin exactly where Sim::spawn_pending_enemies puts them, the player at
    // the centre with four bulkhead partitions in between. If this hangs, the
    // Assault phase hangs.
    SimConfig config;
    VoxelWorld world(config.sector_chunks_x, config.sector_chunks_y, config.sector_chunks_z);
    generate_test_sector(world, config.seed);

    const ivec3 extent = world.size_in_voxels();
    const vec3 player{static_cast<f32>(extent.x) * 0.5f, 3.0f, static_cast<f32>(extent.z) * 0.5f};

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    // One of each kind, all from the origin, as the wave director sends them.
    for (u8 i = 0; i < static_cast<u8>(EnemyKind::Count); ++i) {
        spawn(registry, static_cast<EnemyKind>(i), vec3{0.0f}, 4000.0f);
    }

    i32 arrivals = 0;
    for (i32 i = 0; i < 60 * 90 && arrivals == 0; ++i) {
        arrivals += tick_enemies(registry, world, nav, player, kStep).attacks_landed;
    }
    CHECK(arrivals > 0);
}

TEST_CASE("the roster's body size does not wall the horde into its spawn room") {
    // A regression with teeth. NavField rounds a body up to whole voxel
    // columns and the sector's doorways are three voxels wide, so a body one
    // half-extent too fat fails the stand test in every doorway. There is one
    // field, built for the largest body, so the whole horde then cannot leave
    // the corner it spawned in — and every symptom of that is "the wave never
    // clears", which looks like a navigation bug and is a tuning constant.
    SimConfig config;
    VoxelWorld world(config.sector_chunks_x, config.sector_chunks_y, config.sector_chunks_z);
    generate_test_sector(world, config.seed);

    const ivec3 extent = world.size_in_voxels();
    NavField nav;
    rebuild_for(nav, world, vec3{static_cast<f32>(extent.x) * 0.5f, 3.0f, static_cast<f32>(extent.z) * 0.5f});

    // The corner Sim spawns into, once placed legally on the deck.
    CHECK(nav.reachable(vec3{3.0f, 3.0f, 3.0f}));
    CHECK(nav.reachable_cells() > 2000);
}

TEST_CASE("an enemy pinned on a wall with no way round is reported stuck") {
    // Same room, but the doorway is sealed. Nothing can arrive, and the caller
    // has to be told rather than left to watch enemies_remaining() never move.
    VoxelWorld world = two_rooms();
    for (i32 y = 1; y <= 3; ++y) {
        for (i32 z = 31; z <= 33; ++z) world.set(32, y, z, Voxel::HullPlate);
    }
    const vec3 player = standing_at(EnemyKind::Skitter, 48.0f, 32.5f);

    NavField nav;
    rebuild_for(nav, world, player);
    REQUIRE_FALSE(nav.reachable(standing_at(EnemyKind::Skitter, 8.0f, 8.0f)));

    entt::registry registry;
    spawn(registry, EnemyKind::Skitter, standing_at(EnemyKind::Skitter, 8.0f, 8.0f));

    bool reported = false;
    for (i32 i = 0; i < 60 * 8 && !reported; ++i) {
        reported = tick_enemies(registry, world, nav, player, kStep).any_enemy_stuck;
    }
    CHECK(reported);
}

TEST_CASE("an enemy walking a long way round is not reported stuck for taking a corner") {
    // The guard has to be quiet on a working horde or Sim will act on it every
    // wave. The route through the doorway is a detour of tens of voxels.
    VoxelWorld world = two_rooms();
    const vec3 player = standing_at(EnemyKind::Skitter, 48.0f, 32.5f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    spawn(registry, EnemyKind::Skitter, standing_at(EnemyKind::Skitter, 8.0f, 8.0f));

    bool reported = false;
    for (i32 i = 0; i < 60 * 6; ++i) {
        reported = reported || tick_enemies(registry, world, nav, player, kStep).any_enemy_stuck;
    }
    CHECK_FALSE(reported);
}

TEST_CASE("an enemy that begins buried in the hull digs itself onto the deck") {
    // Sim still spawns the whole wave at the sector origin, which is the corner
    // where the deck and two hull walls meet. Without the first-step placement
    // the horde ejects sideways into vacuum and the wave never clears.
    VoxelWorld world = two_rooms();
    const vec3 player = standing_at(EnemyKind::Skitter, 20.0f, 32.5f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    const entt::entity buried = spawn(registry, EnemyKind::Skitter, vec3{0.0f});

    for (i32 i = 0; i < 60 * 10; ++i) {
        const EnemyTickResult step = tick_enemies(registry, world, nav, player, kStep);
        if (step.attacks_landed > 0) break;
    }

    const vec3 position = registry.get<Transform>(buried).position;
    CHECK(position.x > 0.0f);
    CHECK(position.z > 0.0f);
    CHECK(static_cast<f64>(glm::length(player - position)) < 3.0);
}

// --- the Bulwark's plate ----------------------------------------------------

TEST_CASE("a Bulwark takes far more damage from behind than into its plate") {
    // Facing +Z (yaw 0). A shot into the plate travels -Z; a shot into its back
    // travels +Z. Both directions are asserted because the sign of that dot
    // product is exactly the thing a reader gets backwards.
    const f32 front = armour_multiplier(EnemyKind::Bulwark, 0.0f, vec3{0.0f, 0.0f, -1.0f});
    const f32 rear = armour_multiplier(EnemyKind::Bulwark, 0.0f, vec3{0.0f, 0.0f, 1.0f});
    const f32 side = armour_multiplier(EnemyKind::Bulwark, 0.0f, vec3{1.0f, 0.0f, 0.0f});

    CHECK(static_cast<f64>(front) == doctest::Approx(static_cast<f64>(kBulwarkFrontMultiplier)));
    CHECK(static_cast<f64>(rear) == doctest::Approx(static_cast<f64>(kBulwarkRearMultiplier)));
    CHECK(static_cast<f64>(side) == doctest::Approx(1.0));
    CHECK(rear > side);
    CHECK(side > front);
    // The spread has to be a mechanic, not a stat: flanking must be worth
    // committing to, not worth a footnote.
    CHECK(static_cast<f64>(rear / front) > 3.0);
}

TEST_CASE("the plate follows the Bulwark's facing, not the world axes") {
    // Facing -X (yaw = -pi/2 under forward = {sin y, 0, cos y}).
    constexpr f32 kHalfPi = 1.57079633f;
    const f32 into_plate = armour_multiplier(EnemyKind::Bulwark, -kHalfPi, vec3{1.0f, 0.0f, 0.0f});
    const f32 into_back = armour_multiplier(EnemyKind::Bulwark, -kHalfPi, vec3{-1.0f, 0.0f, 0.0f});
    CHECK(static_cast<f64>(into_plate) == doctest::Approx(static_cast<f64>(kBulwarkFrontMultiplier)));
    CHECK(static_cast<f64>(into_back) == doctest::Approx(static_cast<f64>(kBulwarkRearMultiplier)));
}

TEST_CASE("shooting a Bulwark from above is a flank, not a hole in the model") {
    // Vertical component is discarded, so a shot straight down reads as
    // directionless and takes the neutral multiplier rather than NaN.
    CHECK(static_cast<f64>(armour_multiplier(EnemyKind::Bulwark, 0.0f, vec3{0.0f, -1.0f, 0.0f})) ==
          doctest::Approx(1.0));
    CHECK(static_cast<f64>(armour_multiplier(EnemyKind::Bulwark, 0.0f, vec3{0.0f})) == doctest::Approx(1.0));
}

TEST_CASE("only the Bulwark has a plate") {
    for (u8 i = 0; i < static_cast<u8>(EnemyKind::Count); ++i) {
        const auto kind = static_cast<EnemyKind>(i);
        if (kind == EnemyKind::Bulwark) continue;
        CHECK(static_cast<f64>(armour_multiplier(kind, 0.0f, vec3{0.0f, 0.0f, -1.0f})) ==
              doctest::Approx(1.0));
    }
}

TEST_CASE("damage_enemy routes through the plate and returns what it dealt") {
    entt::registry registry;
    const entt::entity bulwark = spawn(registry, EnemyKind::Bulwark, vec3{10.0f, 3.0f, 10.0f}, 400.0f);

    const f32 into_plate = damage_enemy(registry, bulwark, 100.0f, vec3{0.0f, 0.0f, -1.0f});
    const f32 remaining_after_front = registry.get<Health>(bulwark).current;
    const f32 into_back = damage_enemy(registry, bulwark, 100.0f, vec3{0.0f, 0.0f, 1.0f});

    CHECK(into_back > into_plate);
    CHECK(static_cast<f64>(into_plate) == doctest::Approx(100.0 * static_cast<f64>(kBulwarkFrontMultiplier)));
    CHECK(static_cast<f64>(remaining_after_front) == doctest::Approx(400.0 - static_cast<f64>(into_plate)));

    // A dead or unknown entity absorbs nothing rather than crashing: turrets
    // fire at whatever they targeted last step, and it may be gone.
    registry.destroy(bulwark);
    CHECK(static_cast<f64>(damage_enemy(registry, bulwark, 100.0f, vec3{0.0f, 0.0f, 1.0f})) ==
          doctest::Approx(0.0));
}

// --- the Lancer -------------------------------------------------------------

TEST_CASE("a Lancer does not shoot through a wall") {
    VoxelWorld world = two_rooms();
    // Player and Lancer on opposite sides of the partition, well inside the
    // Lancer's range and nowhere near the doorway.
    const vec3 player = standing_at(EnemyKind::Lancer, 40.0f, 10.0f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    spawn(registry, EnemyKind::Lancer, standing_at(EnemyKind::Lancer, 24.0f, 10.0f));

    f32 total = 0.0f;
    for (i32 i = 0; i < 60; ++i) {
        // One second only: long enough for several shots, short enough that it
        // cannot have walked to the doorway and around.
        total += tick_enemies(registry, world, nav, player, kStep).damage_to_player;
    }
    CHECK(static_cast<f64>(total) == doctest::Approx(0.0));
}

TEST_CASE("a Lancer with a clear line opens fire on its interval") {
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Lancer, 40.0f, 32.0f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    spawn(registry, EnemyKind::Lancer, standing_at(EnemyKind::Lancer, 22.0f, 32.0f));

    i32 shots = 0;
    for (i32 i = 0; i < 60 * 3; ++i) {
        shots += tick_enemies(registry, world, nav, player, kStep).attacks_landed;
    }
    // Three seconds at kLancerInterval is three shots, not 180.
    CHECK(shots >= 2);
    CHECK(shots <= 4);
}

TEST_CASE("a Lancer holds its standoff instead of closing") {
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Lancer, 40.0f, 32.0f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    // Starts far too close for a ranged unit; it should give ground.
    const entt::entity lancer =
        spawn(registry, EnemyKind::Lancer, standing_at(EnemyKind::Lancer, 36.0f, 32.0f));

    for (i32 i = 0; i < 60 * 4; ++i) {
        const EnemyTickResult ignored = tick_enemies(registry, world, nav, player, kStep);
        (void)ignored;
    }

    const f32 distance = glm::length(player - registry.get<Transform>(lancer).position);
    CHECK(static_cast<f64>(distance) > static_cast<f64>(kLancerRange) * 0.6);
    CHECK(static_cast<f64>(distance) <= static_cast<f64>(kLancerRange) + 1.0);
}

// --- the Breacher -----------------------------------------------------------

TEST_CASE("a Breacher detonating removes voxels") {
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Breacher, 30.0f, 32.0f);

    // A barricade beside the player, inside the blast.
    for (i32 y = 1; y <= 3; ++y) world.set(31, y, 32, Voxel::Barricade);
    REQUIRE(is_solid(world.at(31, 2, 32)));

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    const entt::entity breacher =
        spawn(registry, EnemyKind::Breacher, standing_at(EnemyKind::Breacher, 29.0f, 32.0f), 45.0f);

    const EnemyTickResult step = tick_enemies(registry, world, nav, player, kStep);

    CHECK_FALSE(is_solid(world.at(31, 2, 32)));
    CHECK(step.damage_to_player > 0.0f);
    CHECK(step.attacks_landed == 1);
    CHECK_FALSE(registry.get<Health>(breacher).alive());
    // The deck survives. A hole in the floor drops bodies into a fall nothing
    // handles yet, and the flow field cannot express a pit.
    CHECK(is_solid(world.at(29, 0, 32)));
}

TEST_CASE("a Breacher blocked by a barricade blows through it rather than pinning") {
    // The design thesis: fortifications are temporary. A Breacher that only
    // ever explodes on the player leaves a wall standing forever.
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Breacher, 44.0f, 32.0f);

    for (i32 y = 1; y <= 7; ++y) {
        for (i32 z = 0; z < 64; ++z) world.set(36, y, z, Voxel::Barricade);
    }

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    spawn(registry, EnemyKind::Breacher, standing_at(EnemyKind::Breacher, 30.0f, 32.0f), 45.0f);

    for (i32 i = 0; i < 60 * 6; ++i) {
        const EnemyTickResult ignored = tick_enemies(registry, world, nav, player, kStep);
        (void)ignored;
    }

    // Somewhere along the wall a hole was opened. It never touched the player.
    bool breached = false;
    for (i32 z = 0; z < 64 && !breached; ++z) {
        for (i32 y = 1; y <= 3 && !breached; ++y) breached = !is_solid(world.at(36, y, z));
    }
    CHECK(breached);
}

TEST_CASE("a Breacher blast leaves reinforced walls standing and barricades not") {
    // This is what the 2-salvage and 9-salvage prices are for. If one blast
    // took both, Reinforced would be a worse Barricade.
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Breacher, 30.0f, 32.0f);

    world.set(31, 2, 31, Voxel::Barricade);
    world.set(31, 2, 33, Voxel::Reinforced);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    spawn(registry, EnemyKind::Breacher, standing_at(EnemyKind::Breacher, 29.5f, 32.0f), 45.0f);
    const EnemyTickResult ignored = tick_enemies(registry, world, nav, player, kStep);
    (void)ignored;

    CHECK_FALSE(is_solid(world.at(31, 2, 31)));
    CHECK(is_solid(world.at(31, 2, 33)));
    CHECK(static_cast<i32>(voxel_toughness(Voxel::Reinforced)) > static_cast<i32>(kBreachVoxelDamage));
}

// --- the Warden -------------------------------------------------------------

TEST_CASE("a Warden places voxels near the player") {
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Warden, 32.0f, 32.0f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    spawn(registry, EnemyKind::Warden, standing_at(EnemyKind::Warden, 20.0f, 32.0f), 1800.0f);

    i32 solid_before = 0;
    i32 solid_after = 0;
    for (i32 z = 25; z <= 45; ++z) {
        for (i32 y = 1; y <= 5; ++y) {
            for (i32 x = 25; x <= 45; ++x) {
                if (is_solid(world.at(x, y, z))) ++solid_before;
            }
        }
    }

    for (i32 i = 0; i < 60 * 6; ++i) {
        const EnemyTickResult ignored = tick_enemies(registry, world, nav, player, kStep);
        (void)ignored;
    }

    for (i32 z = 25; z <= 45; ++z) {
        for (i32 y = 1; y <= 5; ++y) {
            for (i32 x = 25; x <= 45; ++x) {
                if (is_solid(world.at(x, y, z))) ++solid_after;
            }
        }
    }
    CHECK(solid_after > solid_before);
}

TEST_CASE("a Warden cannot see the player through a wall and so does not build") {
    VoxelWorld world = two_rooms();
    const vec3 player = standing_at(EnemyKind::Warden, 40.0f, 10.0f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    spawn(registry, EnemyKind::Warden, standing_at(EnemyKind::Warden, 24.0f, 10.0f), 1800.0f);

    i32 solid_before = 0;
    for (i32 z = 5; z <= 15; ++z) {
        for (i32 y = 1; y <= 5; ++y) {
            for (i32 x = 34; x <= 50; ++x) {
                if (is_solid(world.at(x, y, z))) ++solid_before;
            }
        }
    }

    for (i32 i = 0; i < 60; ++i) {
        const EnemyTickResult ignored = tick_enemies(registry, world, nav, player, kStep);
        (void)ignored;
    }

    i32 solid_after = 0;
    for (i32 z = 5; z <= 15; ++z) {
        for (i32 y = 1; y <= 5; ++y) {
            for (i32 x = 34; x <= 50; ++x) {
                if (is_solid(world.at(x, y, z))) ++solid_after;
            }
        }
    }
    CHECK(solid_after == solid_before);
}

// --- contact damage ---------------------------------------------------------

TEST_CASE("contact damage is on an interval, not per tick") {
    // A per-tick melee at 60 Hz is 300 damage/second from one Skitter. The
    // player has 100. This is the bug the interval exists to prevent.
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Skitter, 32.0f, 32.0f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    spawn(registry, EnemyKind::Skitter, standing_at(EnemyKind::Skitter, 33.0f, 32.0f));

    f32 total = 0.0f;
    i32 hits = 0;
    for (i32 i = 0; i < 60; ++i) {
        const EnemyTickResult step = tick_enemies(registry, world, nav, player, kStep);
        total += step.damage_to_player;
        hits += step.attacks_landed;
    }

    const EnemyTuning skitter = tuning_for(EnemyKind::Skitter);
    const i32 expected = 1 + static_cast<i32>(1.0f / skitter.attack_interval);
    CHECK(hits >= expected - 1);
    CHECK(hits <= expected + 1);
    CHECK(static_cast<f64>(total) ==
          doctest::Approx(static_cast<f64>(skitter.contact_damage) * static_cast<f64>(hits)));
}

TEST_CASE("contact damage does not scale with the tick rate") {
    // Halving dt must not double the damage. If it does, the sim is dealing
    // damage per step and the number of steps is a device-dependent quantity.
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Skitter, 32.0f, 32.0f);

    NavField nav;
    rebuild_for(nav, world, player);

    auto damage_over = [&](f32 dt, i32 steps) {
        entt::registry registry;
        spawn(registry, EnemyKind::Skitter, standing_at(EnemyKind::Skitter, 33.0f, 32.0f));
        f32 total = 0.0f;
        for (i32 i = 0; i < steps; ++i) {
            total += tick_enemies(registry, world, nav, player, dt).damage_to_player;
        }
        return total;
    };

    const f32 at_60 = damage_over(kStep, 180);          // 3 s
    const f32 at_120 = damage_over(kStep * 0.5f, 360);  // 3 s
    const f32 at_30 = damage_over(kStep * 2.0f, 90);    // 3 s

    CHECK(static_cast<f64>(at_60) == doctest::Approx(static_cast<f64>(at_120)));
    CHECK(static_cast<f64>(at_60) == doctest::Approx(static_cast<f64>(at_30)));
    CHECK(at_60 > 0.0f);
}

TEST_CASE("an enemy out of reach deals nothing") {
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Skitter, 32.0f, 32.0f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    // Well outside contact range but with a clear line, so it is closing —
    // approach must not be an attack.
    spawn(registry, EnemyKind::Skitter, standing_at(EnemyKind::Skitter, 38.0f, 32.0f));

    const EnemyTickResult step = tick_enemies(registry, world, nav, player, kStep);
    CHECK(static_cast<f64>(step.damage_to_player) == doctest::Approx(0.0));
    CHECK(step.attacks_landed == 0);
}

TEST_CASE("a dead enemy stops attacking on the tick it dies") {
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Skitter, 32.0f, 32.0f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    const entt::entity skitter =
        spawn(registry, EnemyKind::Skitter, standing_at(EnemyKind::Skitter, 33.0f, 32.0f));
    registry.get<Health>(skitter).current = 0.0f;

    f32 total = 0.0f;
    for (i32 i = 0; i < 60; ++i) {
        total += tick_enemies(registry, world, nav, player, kStep).damage_to_player;
    }
    CHECK(static_cast<f64>(total) == doctest::Approx(0.0));
}

// --- determinism ------------------------------------------------------------

TEST_CASE("two identical horde simulations produce identical results") {
    // Replays depend on this, and so does every bug report with a seed in it.
    auto run = [](vec3& out_position, f32& out_damage, i32& out_attacks) {
        VoxelWorld world = two_rooms();
        const vec3 player = standing_at(EnemyKind::Skitter, 48.0f, 32.5f);

        NavField nav;
        rebuild_for(nav, world, player);

        entt::registry registry;
        entt::entity last = entt::null;
        for (i32 i = 0; i < 12; ++i) {
            const auto kind = static_cast<EnemyKind>(i % static_cast<i32>(EnemyKind::Count));
            last = spawn(registry, kind, vec3{6.0f + static_cast<f32>(i), 3.0f, 8.0f + static_cast<f32>(i)});
        }

        out_damage = 0.0f;
        out_attacks = 0;
        for (i32 i = 0; i < 60 * 12; ++i) {
            const EnemyTickResult step = tick_enemies(registry, world, nav, player, kStep);
            out_damage += step.damage_to_player;
            out_attacks += step.attacks_landed;
        }
        out_position = registry.get<Transform>(last).position;
    };

    vec3 position_a{0.0f};
    vec3 position_b{0.0f};
    f32 damage_a = 0.0f;
    f32 damage_b = 0.0f;
    i32 attacks_a = 0;
    i32 attacks_b = 0;
    run(position_a, damage_a, attacks_a);
    run(position_b, damage_b, attacks_b);

    // Bit-identical, not approximately equal: an approximate replay is not a
    // replay. doctest::Approx would hide exactly the drift this is here for.
    CHECK(position_a.x == position_b.x);
    CHECK(position_a.y == position_b.y);
    CHECK(position_a.z == position_b.z);
    CHECK(damage_a == damage_b);
    CHECK(attacks_a == attacks_b);
    CHECK(attacks_a > 0);
}

// --- degenerate input -------------------------------------------------------

TEST_CASE("an empty registry ticks to nothing") {
    VoxelWorld world = open_room();
    NavField nav;
    entt::registry registry;

    const EnemyTickResult step = tick_enemies(registry, world, nav, vec3{32.0f, 3.0f, 32.0f}, kStep);
    CHECK(static_cast<f64>(step.damage_to_player) == doctest::Approx(0.0));
    CHECK(step.attacks_landed == 0);
    CHECK_FALSE(step.any_enemy_stuck);
}

TEST_CASE("a zero or negative step advances nothing") {
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Skitter, 32.0f, 32.0f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    const entt::entity skitter =
        spawn(registry, EnemyKind::Skitter, standing_at(EnemyKind::Skitter, 33.0f, 32.0f));
    const vec3 before = registry.get<Transform>(skitter).position;

    for (const f32 dt : {0.0f, -kStep}) {
        const EnemyTickResult step = tick_enemies(registry, world, nav, player, dt);
        CHECK(static_cast<f64>(step.damage_to_player) == doctest::Approx(0.0));
        CHECK(step.attacks_landed == 0);
        CHECK_FALSE(step.any_enemy_stuck);
    }

    const vec3 after = registry.get<Transform>(skitter).position;
    CHECK(after.x == before.x);
    CHECK(after.y == before.y);
    CHECK(after.z == before.z);
}

TEST_CASE("an unbuilt nav field degrades into steering, not into NaN") {
    // NavField::reset() without rebuild() reports everything unreachable. With
    // no route to refine, the last resort is to walk at the player — which in
    // an open room works, and is the reason a missing field is a degraded
    // horde rather than a hung one. Normalising the field's zero vector would
    // put a NaN in a Transform instead, and NaN positions reach the renderer.
    VoxelWorld world = open_room();
    const vec3 player = standing_at(EnemyKind::Skitter, 32.0f, 32.0f);

    NavField nav;
    nav.reset(world.size_in_voxels());

    entt::registry registry;
    const entt::entity skitter =
        spawn(registry, EnemyKind::Skitter, standing_at(EnemyKind::Skitter, 10.0f, 10.0f));

    i32 attacks = 0;
    for (i32 i = 0; i < 60 * 6; ++i) {
        attacks += tick_enemies(registry, world, nav, player, kStep).attacks_landed;
    }

    const vec3 position = registry.get<Transform>(skitter).position;
    CHECK(std::isfinite(position.x));
    CHECK(std::isfinite(position.y));
    CHECK(std::isfinite(position.z));
    CHECK(attacks > 0);
}

TEST_CASE("a player position outside the sector is survivable") {
    VoxelWorld world = open_room();
    NavField nav;
    rebuild_for(nav, world, vec3{32.0f, 3.0f, 32.0f});

    entt::registry registry;
    const entt::entity skitter =
        spawn(registry, EnemyKind::Skitter, standing_at(EnemyKind::Skitter, 10.0f, 10.0f));

    for (i32 i = 0; i < 120; ++i) {
        const EnemyTickResult ignored =
            tick_enemies(registry, world, nav, vec3{-4000.0f, 3.0f, -4000.0f}, kStep);
        (void)ignored;
    }

    const vec3 position = registry.get<Transform>(skitter).position;
    CHECK(std::isfinite(position.x));
    CHECK(std::isfinite(position.z));
}

// --- the roster -------------------------------------------------------------

TEST_CASE("every kind fits through a generated doorway") {
    // generate_test_sector punches openings three voxels tall. A body taller
    // than that is walled into the room it spawned in no matter how good the
    // navigation is, which is a soft-lock that no test of the flow field would
    // ever catch.
    for (u8 i = 0; i < static_cast<u8>(EnemyKind::Count); ++i) {
        const EnemyTuning tuning = tuning_for(static_cast<EnemyKind>(i));
        CAPTURE(i);
        CHECK(tuning.half_extents.y <= kMaxDoorwayHalfHeight);
        CHECK(static_cast<f64>(2.0f * tuning.half_extents.x) <= 3.0);
        CHECK(static_cast<f64>(2.0f * tuning.half_extents.z) <= 3.0);
        CHECK(tuning.half_extents.x > 0.0f);
    }
}

TEST_CASE("the nav body is the largest body in the roster") {
    // One field, five sizes: it has to be built for the biggest, or the field
    // routes the Warden through a gap it does not fit.
    const vec3 body = nav_body_half_extents();
    for (u8 i = 0; i < static_cast<u8>(EnemyKind::Count); ++i) {
        const vec3 half = tuning_for(static_cast<EnemyKind>(i)).half_extents;
        CHECK(half.x <= body.x);
        CHECK(half.y <= body.y);
        CHECK(half.z <= body.z);
    }
}

TEST_CASE("only the Skitter outruns the player") {
    // A horde where everything is faster than you is not a horde, it is a
    // cutscene. kMoveSpeed in app.cpp is 11 voxels/s.
    constexpr f32 kPlayerSpeed = 11.0f;
    CHECK(tuning_for(EnemyKind::Skitter).move_speed > kPlayerSpeed);
    CHECK(tuning_for(EnemyKind::Lancer).move_speed < kPlayerSpeed);
    CHECK(tuning_for(EnemyKind::Bulwark).move_speed < kPlayerSpeed);
    CHECK(tuning_for(EnemyKind::Breacher).move_speed < kPlayerSpeed);
    CHECK(tuning_for(EnemyKind::Warden).move_speed < kPlayerSpeed);
}

TEST_CASE("the kinds are actually distinct") {
    // If two archetypes tune the same, the horde is wallpaper.
    const EnemyTuning skitter = tuning_for(EnemyKind::Skitter);
    const EnemyTuning bulwark = tuning_for(EnemyKind::Bulwark);
    CHECK(skitter.move_speed > bulwark.move_speed * 2.0f);
    CHECK(bulwark.contact_damage > skitter.contact_damage * 2.0f);
    CHECK(bulwark.turn_rate < skitter.turn_rate);
    CHECK(static_cast<f64>(tuning_for(EnemyKind::Lancer).preferred_range) > 0.0);
    CHECK(static_cast<f64>(skitter.preferred_range) == doctest::Approx(0.0));
}

// --- scale ------------------------------------------------------------------

TEST_CASE("a full roster of enemies ticks without anything going non-finite") {
    // kMaxRosterSize bodies, the shipping ceiling, in one step.
    VoxelWorld world = two_rooms();
    const vec3 player = standing_at(EnemyKind::Skitter, 48.0f, 32.5f);

    NavField nav;
    rebuild_for(nav, world, player);

    entt::registry registry;
    for (usize i = 0; i < WaveDirector::kMaxRosterSize; ++i) {
        const auto kind = static_cast<EnemyKind>(i % static_cast<usize>(EnemyKind::Count));
        const auto offset = static_cast<f32>(i % 20);
        spawn(registry, kind, vec3{4.0f + offset, 3.0f, 4.0f + static_cast<f32>(i / 20)});
    }

    for (i32 i = 0; i < 120; ++i) {
        const EnemyTickResult ignored = tick_enemies(registry, world, nav, player, kStep);
        (void)ignored;
    }

    for (const entt::entity entity : registry.view<const Transform, const Enemy>()) {
        const vec3 position = registry.get<const Transform>(entity).position;
        REQUIRE(std::isfinite(position.x));
        REQUIRE(std::isfinite(position.y));
        REQUIRE(std::isfinite(position.z));
    }
}
