// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "game/wave_director.hpp"

#include <doctest/doctest.h>

using namespace df;

TEST_CASE("the difficulty budget rises every wave") {
    for (i32 w = 1; w < 40; ++w) {
        CHECK(WaveDirector::budget_for(w + 1) > WaveDirector::budget_for(w));
    }
}

TEST_CASE("prep time shrinks but never disappears") {
    for (i32 w = 1; w < 60; ++w) {
        const f32 t = WaveDirector::prep_time_for(w);
        CHECK(t >= 22.0f);
        CHECK(t <= 75.0f);
    }
    CHECK(WaveDirector::prep_time_for(2) > WaveDirector::prep_time_for(8));
}

TEST_CASE("the same seed always produces the same wave") {
    WaveDirector a(99);
    WaveDirector b(99);
    for (i32 w = 1; w <= 12; ++w) {
        CHECK(a.spec_for(w).roster == b.spec_for(w).roster);
    }
}

TEST_CASE("different seeds produce different waves") {
    WaveDirector a(1);
    WaveDirector b(2);
    bool any_difference = false;
    for (i32 w = 1; w <= 12; ++w) {
        if (a.spec_for(w).roster != b.spec_for(w).roster) any_difference = true;
    }
    CHECK(any_difference);
}

TEST_CASE("a wave can be inspected without simulating the ones before it") {
    // spec_for is pure, so a balance tool can jump straight to wave 30.
    WaveDirector director(5);
    const WaveSpec direct = director.spec_for(30);
    for (i32 w = 1; w < 30; ++w) (void)director.spec_for(w);
    CHECK(director.spec_for(30).roster == direct.roster);
}

TEST_CASE("early waves are skitters only, so the roster teaches itself") {
    WaveDirector director(7);
    for (i32 w = 1; w <= 2; ++w) {
        const WaveSpec spec = director.spec_for(w);
        REQUIRE_FALSE(spec.roster.empty());
        CHECK(spec.count_of(EnemyKind::Skitter) == static_cast<i32>(spec.size()));
    }
}

TEST_CASE("archetypes unlock on schedule and stay unlocked") {
    WaveDirector director(11);
    CHECK(director.spec_for(2).count_of(EnemyKind::Lancer) == 0);
    CHECK(director.spec_for(2).count_of(EnemyKind::Breacher) == 0);
    CHECK(director.spec_for(4).count_of(EnemyKind::Bulwark) == 0);

    // By wave 12 every archetype should have appeared at least once across a
    // few seeds — the roster is random, so sample rather than assert one wave.
    int seen_bulwark = 0;
    for (u32 seed = 0; seed < 8; ++seed) {
        WaveDirector d(seed);
        if (d.spec_for(12).count_of(EnemyKind::Bulwark) > 0) ++seen_bulwark;
    }
    CHECK(seen_bulwark > 0);
}

TEST_CASE("boss waves land every five waves and lead with the boss") {
    WaveDirector director(3);
    for (i32 w = 1; w <= 25; ++w) {
        const WaveSpec spec = director.spec_for(w);
        const bool expected_boss = w % WaveDirector::kBossEvery == 0;
        CHECK(spec.boss_wave == expected_boss);
        CHECK(spec.count_of(EnemyKind::Warden) == (expected_boss ? 1 : 0));
        if (expected_boss) {
            REQUIRE_FALSE(spec.roster.empty());
            CHECK(spec.roster.front() == EnemyKind::Warden);
            // A boss must arrive with a real escort, not alone.
            CHECK(spec.size() > 5);
        }
    }
}

TEST_CASE("wave composition always spends its whole budget") {
    WaveDirector director(42);
    for (i32 w = 1; w <= 40; ++w) {
        const WaveSpec spec = director.spec_for(w);
        REQUIRE_FALSE(spec.roster.empty());

        f32 spent = 0.0f;
        for (const EnemyKind kind : spec.roster) spent += enemy_cost(kind);

        // Budget the roster cap refused is converted into elite scaling
        // instead, so bodies * power must still account for the whole budget.
        const f32 effective = spent * spec.elite_multiplier;
        INFO("wave " << w << ": " << spec.size() << " bodies x " << spec.elite_multiplier << " = "
                     << effective << " of " << spec.budget);
        CHECK(effective >= spec.budget * 0.85f);
    }
}

TEST_CASE("wave size never exceeds what the frame budget can hold") {
    for (u32 seed = 0; seed < 6; ++seed) {
        WaveDirector director(seed);
        for (i32 w = 1; w <= 60; ++w) {
            // Blow this and the draw-call and pathfinding budgets go long
            // before the player is having a good time.
            CHECK(director.spec_for(w).size() <= WaveDirector::kMaxRosterSize);
        }
    }
}

TEST_CASE("late waves get harder through elites, not through more bodies") {
    WaveDirector director(13);

    // Early waves are well under the cap, so no elite scaling is needed.
    CHECK(director.spec_for(3).elite_multiplier == doctest::Approx(1.0f));

    // Deep waves saturate the cap and must escalate by power instead.
    const WaveSpec deep = director.spec_for(50);
    CHECK(deep.size() == WaveDirector::kMaxRosterSize);
    CHECK(deep.elite_multiplier > 1.5f);

    // And that escalation must itself keep rising.
    CHECK(director.spec_for(55).elite_multiplier > deep.elite_multiplier);
}
