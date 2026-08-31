// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/frame_stats.hpp"
#include "core/perf_budget.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace df;

namespace {

/// Feeds `count` frames of a fixed millisecond duration.
void push_ms(FrameStats& stats, f64 ms, usize count) {
    for (usize i = 0; i < count; ++i) stats.push(ms / 1000.0);
}

}  // namespace

TEST_CASE("an empty window reports zero rather than dividing by nothing") {
    const FrameStats stats;
    CHECK(stats.sample_count() == 0);
    CHECK(stats.mean_ms() == doctest::Approx(0.0));
    CHECK(stats.min_ms() == doctest::Approx(0.0));
    CHECK(stats.max_ms() == doctest::Approx(0.0));
    CHECK(stats.last_ms() == doctest::Approx(0.0));
    CHECK(stats.worst_ms() == doctest::Approx(0.0));
    CHECK(stats.percentile_ms(0.95) == doctest::Approx(0.0));
    CHECK(stats.fps() == doctest::Approx(0.0));
}

TEST_CASE("mean, min, max and last describe the window") {
    FrameStats stats;
    stats.push(0.010);
    stats.push(0.020);
    stats.push(0.030);

    CHECK(stats.sample_count() == 3);
    CHECK(stats.mean_ms() == doctest::Approx(20.0));
    CHECK(stats.min_ms() == doctest::Approx(10.0));
    CHECK(stats.max_ms() == doctest::Approx(30.0));
    CHECK(stats.last_ms() == doctest::Approx(30.0));
}

TEST_CASE("fps is the reciprocal of the mean frame time") {
    FrameStats stats;
    push_ms(stats, budget::kFrameMs, 10);
    CHECK(stats.fps() == doctest::Approx(60.0).epsilon(0.001));
}

TEST_CASE("an average alone hides a stuttering window") {
    // The case the percentile exists for: 8 ms and 25 ms alternating averages
    // to a comfortable 16.5 ms while feeling awful. The mean says fine, the
    // percentile says otherwise, and only one of them is right.
    FrameStats stats;
    for (int i = 0; i < 60; ++i) {
        stats.push(0.008);
        stats.push(0.025);
    }

    CHECK(stats.mean_ms() == doctest::Approx(16.5));
    CHECK(stats.mean_ms() < budget::kFrameMs);
    CHECK(stats.percentile_ms(0.95) > budget::kFrameMs);
}

TEST_CASE("percentiles are nearest-rank over the window") {
    FrameStats stats;
    // 1..100 ms, so the nth percentile is n ms exactly.
    for (int i = 1; i <= 100; ++i) stats.push(static_cast<f64>(i) / 1000.0);

    CHECK(stats.percentile_ms(0.50) == doctest::Approx(50.0));
    CHECK(stats.percentile_ms(0.95) == doctest::Approx(95.0));
    CHECK(stats.percentile_ms(0.99) == doctest::Approx(99.0));
    CHECK(stats.percentile_ms(1.0) == doctest::Approx(stats.max_ms()));
    CHECK(stats.percentile_ms(0.0) == doctest::Approx(stats.min_ms()));
}

TEST_CASE("percentile fractions outside [0,1] are clamped, not undefined") {
    FrameStats stats;
    push_ms(stats, 16.0, 10);
    CHECK(stats.percentile_ms(-1.0) == doctest::Approx(stats.min_ms()));
    CHECK(stats.percentile_ms(5.0) == doctest::Approx(stats.max_ms()));
}

TEST_CASE("the window forgets frames older than kWindow") {
    FrameStats stats;
    push_ms(stats, 100.0, FrameStats::kWindow);
    CHECK(stats.mean_ms() == doctest::Approx(100.0));

    // Overwrite every slot. The old, slow frames must be gone from the window.
    push_ms(stats, 10.0, FrameStats::kWindow);
    CHECK(stats.sample_count() == FrameStats::kWindow);
    CHECK(stats.mean_ms() == doctest::Approx(10.0));
    CHECK(stats.max_ms() == doctest::Approx(10.0));
}

TEST_CASE("the high-water mark outlives the window") {
    // Thermal drift and one-off hitches are exactly what a short window
    // forgets, so worst_ms is deliberately not windowed.
    FrameStats stats;
    stats.push(0.120);
    push_ms(stats, 10.0, FrameStats::kWindow);

    CHECK(stats.max_ms() == doctest::Approx(10.0));
    CHECK(stats.worst_ms() == doctest::Approx(120.0));
}

TEST_CASE("a backgrounded app is a stall, not a slow frame") {
    FrameStats stats;
    push_ms(stats, 16.0, 10);

    // Tab hidden for four seconds. Folding this into the window would poison
    // every figure for the next four seconds of real play.
    stats.push(4.0);

    CHECK(stats.stall_count() == 1);
    CHECK(stats.sample_count() == 10);
    CHECK(stats.mean_ms() == doctest::Approx(16.0));
    CHECK(stats.max_ms() == doctest::Approx(16.0));
    CHECK(stats.worst_ms() == doctest::Approx(16.0));
    // It still happened, so it still counts as a frame.
    CHECK(stats.total_frames() == 11);
}

TEST_CASE("a frame just under the stall threshold is still a frame") {
    FrameStats stats;
    stats.push(FrameStats::kStallSeconds - 0.001);
    CHECK(stats.stall_count() == 0);
    CHECK(stats.sample_count() == 1);
}

TEST_CASE("a lying clock is ignored outright") {
    FrameStats stats;
    push_ms(stats, 16.0, 4);

    stats.push(-1.0);
    stats.push(std::numeric_limits<f64>::quiet_NaN());
    stats.push(std::numeric_limits<f64>::infinity());

    CHECK(stats.sample_count() == 4);
    CHECK(stats.total_frames() == 4);
    CHECK(stats.mean_ms() == doctest::Approx(16.0));
    CHECK(std::isfinite(stats.mean_ms()));
    CHECK(std::isfinite(stats.worst_ms()));
}

TEST_CASE("dropped simulation steps are counted separately from stalls") {
    // They look the same from outside and mean completely different things:
    // a dropped step is the device failing to keep up, a stall is the app not
    // running at all.
    FrameStats stats;
    stats.push(0.016, false);
    stats.push(0.050, true);
    stats.push(0.050, true);
    stats.push(2.0, false);

    CHECK(stats.dropped_step_frames() == 2);
    CHECK(stats.stall_count() == 1);
    CHECK(stats.total_frames() == 4);
}

TEST_CASE("reset clears the window and every counter") {
    FrameStats stats;
    stats.push(0.016, true);
    stats.push(3.0);
    stats.reset();

    CHECK(stats.sample_count() == 0);
    CHECK(stats.total_frames() == 0);
    CHECK(stats.stall_count() == 0);
    CHECK(stats.dropped_step_frames() == 0);
    CHECK(stats.worst_ms() == doctest::Approx(0.0));
    CHECK(stats.mean_ms() == doctest::Approx(0.0));
}

TEST_CASE("a zero-length frame does not produce an infinite frame rate") {
    FrameStats stats;
    stats.push(0.0);
    CHECK(stats.sample_count() == 1);
    CHECK(std::isfinite(stats.fps()));
    CHECK(stats.fps() == doctest::Approx(0.0));
}

TEST_CASE("the performance budget matches the one the README publishes") {
    // The HUD colours everything against these, so a typo here silently turns
    // the instrument into decoration.
    CHECK(budget::kFrameMs == doctest::Approx(1000.0 / 60.0));
    CHECK(budget::kRemeshMs == doctest::Approx(2.5));
    CHECK(budget::kRemeshChunks == 4);
    CHECK(budget::kDrawCalls == 300);
    CHECK(budget::kResidentBytes == 700ull * 1024ull * 1024ull);
    CHECK(budget::kWarnFraction > 0.0);
    CHECK(budget::kWarnFraction < 1.0);
}
