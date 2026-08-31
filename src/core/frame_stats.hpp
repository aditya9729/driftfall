// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

#include <array>

namespace df {

/// A rolling window of frame times, and the summary the debug HUD reads.
///
/// Deliberately headless and free of any graphics type, so the thing the HUD
/// reports is unit-tested rather than eyeballed. An average alone is close to
/// useless for a shooter: a run that alternates 8 ms and 25 ms averages to a
/// comfortable 16.5 ms while feeling terrible, which is why the percentile and
/// the high-water mark are here too.
///
/// The long-horizon figure that actually matters on a phone is worst_ms(),
/// which is not windowed. Thermal throttling shows up as the window slowly
/// degrading over ten minutes, and a four-second window forgets that by design.
class FrameStats {
public:
    /// Four seconds at 60 Hz. Long enough for a percentile to mean something,
    /// short enough that the numbers still track what you are doing right now.
    static constexpr usize kWindow = 240;

    /// A frame this long is not a slow frame — it is the app not running.
    /// A backgrounded browser tab, a suspended app, or a debugger breakpoint
    /// all produce one, and folding it into the window would poison the
    /// maximum for the next four seconds. Counted separately instead.
    static constexpr f64 kStallSeconds = 0.5;

    /// Records one frame. Non-finite and negative deltas are ignored outright;
    /// deltas of kStallSeconds or more are counted as stalls and kept out of
    /// the window.
    void push(f64 frame_seconds, bool dropped_sim_steps = false);

    void reset();

    [[nodiscard]] usize sample_count() const { return count_; }

    [[nodiscard]] u64 total_frames() const { return total_frames_; }

    [[nodiscard]] u64 stall_count() const { return stalls_; }

    [[nodiscard]] u64 dropped_step_frames() const { return dropped_step_frames_; }

    /// Milliseconds. All of these return 0 with an empty window.
    [[nodiscard]] f64 last_ms() const;
    [[nodiscard]] f64 mean_ms() const;
    [[nodiscard]] f64 min_ms() const;
    [[nodiscard]] f64 max_ms() const;

    /// Nearest-rank percentile over the window. `fraction` is clamped to
    /// [0, 1]; percentile_ms(1.0) is max_ms().
    [[nodiscard]] f64 percentile_ms(f64 fraction) const;

    /// Slowest non-stall frame since the last reset. Not windowed: this is the
    /// number that catches a hitch you looked away from.
    [[nodiscard]] f64 worst_ms() const { return worst_ms_; }

    /// Frames per second implied by the window mean. 0 when there is no data.
    [[nodiscard]] f64 fps() const;

private:
    std::array<f64, kWindow> samples_{};
    usize head_ = 0;   ///< where the next sample goes
    usize count_ = 0;  ///< how many of samples_ are live, saturating at kWindow
    u64 total_frames_ = 0;
    u64 stalls_ = 0;
    u64 dropped_step_frames_ = 0;
    f64 worst_ms_ = 0.0;
};

}  // namespace df
