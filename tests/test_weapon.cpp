// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "game/weapon.hpp"

#include <doctest/doctest.h>

using namespace df;

namespace {

/// Advance a weapon to a given fraction of its reload, in sim-sized steps so
/// the test exercises the same code path the game does.
void advance_reload_to(Weapon& weapon, f32 fraction) {
    const f32 step = 1.0f / 60.0f;
    while (weapon.reload_progress() < fraction && weapon.state() == ReloadState::Reloading) {
        weapon.tick(step);
    }
}

}  // namespace

TEST_CASE("a weapon starts loaded") {
    Weapon weapon(WeaponStats::salvage_rifle());
    CHECK(weapon.ammo() == weapon.stats().mag_size);
    CHECK(weapon.state() == ReloadState::Ready);
}

TEST_CASE("firing consumes ammo and respects the fire rate") {
    Weapon weapon(WeaponStats::salvage_rifle());

    CHECK(weapon.fire() > 0.0f);
    CHECK(weapon.ammo() == weapon.stats().mag_size - 1);

    // Immediately again: still on cooldown.
    CHECK(weapon.fire() == 0.0f);
    CHECK(weapon.ammo() == weapon.stats().mag_size - 1);

    weapon.tick(weapon.stats().fire_interval);
    CHECK(weapon.fire() > 0.0f);
}

TEST_CASE("an empty magazine cannot fire") {
    Weapon weapon(WeaponStats::salvage_rifle());
    for (int i = 0; i < weapon.stats().mag_size; ++i) {
        weapon.tick(weapon.stats().fire_interval);
        weapon.fire();
    }
    CHECK(weapon.ammo() == 0);
    weapon.tick(1.0f);
    CHECK(weapon.fire() == 0.0f);
}

TEST_CASE("a full reload completes on its own with no tap") {
    Weapon weapon(WeaponStats::salvage_rifle());
    weapon.tick(weapon.stats().fire_interval);
    weapon.fire();
    weapon.begin_reload();
    CHECK(weapon.state() == ReloadState::Reloading);

    for (int i = 0; i < 300 && weapon.state() != ReloadState::Ready; ++i) weapon.tick(1.0f / 60.0f);

    CHECK(weapon.state() == ReloadState::Ready);
    CHECK(weapon.ammo() == weapon.stats().mag_size);
    CHECK_FALSE(weapon.boosted());
}

TEST_CASE("tapping inside the perfect window boosts the magazine") {
    Weapon weapon(WeaponStats::salvage_rifle());
    weapon.tick(weapon.stats().fire_interval);
    const f32 base_damage = weapon.fire();
    weapon.begin_reload();

    advance_reload_to(weapon, weapon.stats().perfect_start);
    CHECK(weapon.tap_reload() == ReloadResult::Perfect);

    CHECK(weapon.state() == ReloadState::Ready);
    CHECK(weapon.ammo() == weapon.stats().mag_size);
    CHECK(weapon.boosted());

    weapon.tick(weapon.stats().fire_interval);
    CHECK(weapon.fire() > base_damage);
}

TEST_CASE("tapping in the good window reloads fast but grants no bonus") {
    Weapon weapon(WeaponStats::salvage_rifle());
    weapon.tick(weapon.stats().fire_interval);
    weapon.fire();
    weapon.begin_reload();

    advance_reload_to(weapon, weapon.stats().perfect_end + 0.02f);
    CHECK(weapon.tap_reload() == ReloadResult::Good);
    CHECK(weapon.state() == ReloadState::Ready);
    CHECK_FALSE(weapon.boosted());
}

TEST_CASE("tapping too early jams and stretches the reload") {
    WeaponStats stats = WeaponStats::salvage_rifle();
    Weapon weapon(stats);
    weapon.tick(stats.fire_interval);
    weapon.fire();
    weapon.begin_reload();

    weapon.tick(0.05f);  // barely started
    CHECK(weapon.tap_reload() == ReloadResult::Jammed);
    CHECK(weapon.state() == ReloadState::Reloading);

    // It still finishes — a jam is a punishment, never a dead end.
    f32 elapsed = 0.05f;
    for (int i = 0; i < 600 && weapon.state() != ReloadState::Ready; ++i) {
        weapon.tick(1.0f / 60.0f);
        elapsed += 1.0f / 60.0f;
    }
    CHECK(weapon.state() == ReloadState::Ready);
    CHECK(elapsed > stats.reload_seconds);
}

TEST_CASE("mashing reload cannot farm perfects") {
    Weapon weapon(WeaponStats::salvage_rifle());
    weapon.tick(weapon.stats().fire_interval);
    weapon.fire();
    weapon.begin_reload();

    CHECK(weapon.tap_reload() == ReloadResult::Jammed);  // first tap, too early
    // Every later tap in the same reload must be ignored, including one that
    // would otherwise land in the perfect window.
    advance_reload_to(weapon, weapon.stats().perfect_start);
    CHECK(weapon.tap_reload() == ReloadResult::Ignored);
    CHECK_FALSE(weapon.boosted());
}

TEST_CASE("tapping when not reloading is ignored") {
    Weapon weapon(WeaponStats::salvage_rifle());
    CHECK(weapon.tap_reload() == ReloadResult::Ignored);
}

TEST_CASE("reloading a full magazine is refused") {
    Weapon weapon(WeaponStats::salvage_rifle());
    weapon.begin_reload();
    CHECK(weapon.state() == ReloadState::Ready);
}

TEST_CASE("running the magazine dry clears the perfect bonus") {
    Weapon weapon(WeaponStats::salvage_rifle());
    weapon.tick(weapon.stats().fire_interval);
    weapon.fire();
    weapon.begin_reload();
    advance_reload_to(weapon, weapon.stats().perfect_start);
    REQUIRE(weapon.tap_reload() == ReloadResult::Perfect);
    REQUIRE(weapon.boosted());

    for (int i = 0; i < weapon.stats().mag_size; ++i) {
        weapon.tick(weapon.stats().fire_interval);
        weapon.fire();
    }
    CHECK(weapon.ammo() == 0);
    CHECK_FALSE(weapon.boosted());
}

TEST_CASE("every weapon's reload windows are ordered and reachable") {
    const WeaponStats all[] = {
        WeaponStats::salvage_rifle(), WeaponStats::breach_shotgun(), WeaponStats::mining_lance()};
    for (const WeaponStats& stats : all) {
        CHECK(stats.perfect_start > 0.0f);
        CHECK(stats.perfect_start < stats.perfect_end);
        CHECK(stats.perfect_end < stats.good_end);
        CHECK(stats.good_end < 1.0f);
        // The perfect window must be hittable by a human. At 60 Hz, anything
        // under ~4 frames is a coin flip rather than a skill.
        const f32 window_seconds = (stats.perfect_end - stats.perfect_start) * stats.reload_seconds;
        CHECK(window_seconds > 4.0f / 60.0f);
    }
}

TEST_CASE("the mining lance trades enemy damage for voxel damage") {
    const WeaponStats lance = WeaponStats::mining_lance();
    const WeaponStats rifle = WeaponStats::salvage_rifle();
    CHECK(lance.damage < rifle.damage);
    CHECK(lance.voxel_damage > rifle.voxel_damage);
}
