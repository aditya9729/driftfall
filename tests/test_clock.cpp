// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/clock.hpp"

#include <doctest/doctest.h>

using namespace df;

TEST_CASE("fixed timestep runs exactly one step at 60 Hz") {
    FixedTimestep ts;
    CHECK(ts.advance(FixedTimestep::kStep) == 1);
}

TEST_CASE("fixed timestep accumulates fractional frames without drift") {
    FixedTimestep ts;
    // A 120 Hz display: every other frame should produce a sim step.
    int total = 0;
    for (int i = 0; i < 120; ++i) total += ts.advance(1.0 / 120.0);
    // 120 frames of 1/120 s is one second, which is 60 steps.
    CHECK(total == 60);
}

TEST_CASE("fixed timestep refuses to spiral on a huge frame") {
    FixedTimestep ts;
    // Two full seconds of stall — app resumed from background, say.
    const int steps = ts.advance(2.0);
    CHECK(steps <= FixedTimestep::kMaxStepsPerFrame);
    CHECK(ts.dropped_steps());

    // The backlog must be discarded, not carried into the next frame.
    CHECK(ts.advance(0.0) == 0);
}

TEST_CASE("fixed timestep clamps negative deltas") {
    FixedTimestep ts;
    CHECK(ts.advance(-5.0) == 0);
}

TEST_CASE("interpolation alpha stays in range") {
    FixedTimestep ts;
    for (int i = 0; i < 200; ++i) {
        ts.advance(0.0071);
        CHECK(ts.alpha() >= 0.0f);
        CHECK(ts.alpha() < 1.0f);
    }
}
