// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/frame_stats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace df {

void FrameStats::push(f64 frame_seconds, bool dropped_sim_steps) {
    // A negative or NaN delta means the clock lied. There is nothing useful to
    // record and averaging it in would corrupt every figure downstream.
    if (!std::isfinite(frame_seconds) || frame_seconds < 0.0) return;

    ++total_frames_;
    if (dropped_sim_steps) ++dropped_step_frames_;

    if (frame_seconds >= kStallSeconds) {
        ++stalls_;
        return;
    }

    const f64 ms = frame_seconds * 1000.0;
    samples_[head_] = ms;
    head_ = (head_ + 1) % kWindow;
    if (count_ < kWindow) ++count_;

    worst_ms_ = std::max(worst_ms_, ms);
}

void FrameStats::reset() {
    samples_.fill(0.0);
    head_ = 0;
    count_ = 0;
    total_frames_ = 0;
    stalls_ = 0;
    dropped_step_frames_ = 0;
    worst_ms_ = 0.0;
}

f64 FrameStats::last_ms() const {
    if (count_ == 0) return 0.0;
    return samples_[(head_ + kWindow - 1) % kWindow];
}

f64 FrameStats::mean_ms() const {
    if (count_ == 0) return 0.0;
    // The window is a ring, but the live samples always occupy [0, count_)
    // once it has wrapped, and [0, count_) before it has. Either way this is
    // the whole live set.
    const f64 sum = std::accumulate(samples_.begin(), samples_.begin() + static_cast<i64>(count_), 0.0);
    return sum / static_cast<f64>(count_);
}

f64 FrameStats::min_ms() const {
    if (count_ == 0) return 0.0;
    return *std::min_element(samples_.begin(), samples_.begin() + static_cast<i64>(count_));
}

f64 FrameStats::max_ms() const {
    if (count_ == 0) return 0.0;
    return *std::max_element(samples_.begin(), samples_.begin() + static_cast<i64>(count_));
}

f64 FrameStats::percentile_ms(f64 fraction) const {
    if (count_ == 0) return 0.0;

    fraction = std::clamp(fraction, 0.0, 1.0);

    // Nearest-rank: the smallest value at or above the requested fraction of
    // the sorted window. nth_element on a stack copy keeps this allocation-free
    // and linear, which matters because the HUD asks for it every frame.
    std::array<f64, kWindow> scratch{};
    std::copy(samples_.begin(), samples_.begin() + static_cast<i64>(count_), scratch.begin());

    const auto rank = static_cast<i64>(std::ceil(fraction * static_cast<f64>(count_)));
    const i64 index = std::clamp<i64>(rank - 1, 0, static_cast<i64>(count_) - 1);

    std::nth_element(scratch.begin(), scratch.begin() + index, scratch.begin() + static_cast<i64>(count_));
    return scratch[static_cast<usize>(index)];
}

f64 FrameStats::fps() const {
    const f64 mean = mean_ms();
    if (mean <= 0.0) return 0.0;
    return 1000.0 / mean;
}

}  // namespace df
