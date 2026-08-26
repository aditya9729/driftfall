// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/clock.hpp"

#include <algorithm>
#include <chrono>

namespace df {

int FixedTimestep::advance(f64 frame_delta_seconds) {
    // A single absurd delta (tab backgrounded, app resumed from suspend,
    // debugger breakpoint) must not translate into a burst of simulation.
    frame_delta_seconds = std::clamp(frame_delta_seconds, 0.0, 0.25);

    accumulator_ += frame_delta_seconds;

    int steps = 0;
    while (accumulator_ >= kStep && steps < kMaxStepsPerFrame) {
        accumulator_ -= kStep;
        ++steps;
    }

    dropped_ = accumulator_ >= kStep;
    if (dropped_) {
        // Discard the backlog instead of carrying it. Carrying it means the
        // next frame inherits the debt and the game runs in slow motion.
        accumulator_ = 0.0;
    }
    return steps;
}

f64 now_seconds() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point origin = clock::now();
    return std::chrono::duration<f64>(clock::now() - origin).count();
}

}  // namespace df
