// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

namespace df {

/// Fixed-timestep accumulator.
///
/// The simulation runs at a fixed 60 Hz regardless of display rate. Phones
/// hand us wildly variable frame times — 120 Hz ProMotion one second, 22 Hz
/// mid-thermal-throttle the next — and a variable-dt simulation makes gunplay,
/// recoil, and the active-reload window feel different on every device. That
/// is unacceptable for a shooter, so we decouple.
///
/// Rendering interpolates between the last two sim states using alpha().
class FixedTimestep {
public:
    static constexpr f64 kSimHz = 60.0;
    static constexpr f64 kStep = 1.0 / kSimHz;

    /// Never simulate more than this many steps for one frame. Past this we
    /// deliberately let the simulation fall behind wall-clock rather than
    /// enter a spiral of death where catching up costs more than it recovers.
    static constexpr int kMaxStepsPerFrame = 5;

    /// Feed wall-clock delta; returns how many fixed steps to run this frame.
    int advance(f64 frame_delta_seconds);

    /// Interpolation factor in [0,1) between the previous and current sim state.
    [[nodiscard]] f32 alpha() const { return static_cast<f32>(accumulator_ / kStep); }

    /// True when advance() clamped, i.e. the device could not keep up.
    [[nodiscard]] bool dropped_steps() const { return dropped_; }

private:
    f64 accumulator_ = 0.0;
    bool dropped_ = false;
};

/// Monotonic wall-clock seconds since process start. Safe on Emscripten.
f64 now_seconds();

}  // namespace df
