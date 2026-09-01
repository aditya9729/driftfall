// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/frame_stats.hpp"
#include "core/mem_stats.hpp"
#include "core/types.hpp"
#include "render/renderer.hpp"

namespace df {

/// The on-screen performance readout.
///
/// M1's proof is "you can fly through a sector on your phone at a locked 60
/// fps, and the HUD proves it" — so this is not a developer nicety, it is the
/// instrument the milestone is measured with. Every figure is coloured against
/// the matching constant in core/perf_budget.hpp: green inside budget, amber
/// approaching it, red over. Watching amber creep in over ten minutes is how
/// thermal throttling announces itself.
///
/// Drawn with bgfx's built-in debug text, which costs one extra view and no
/// font atlas, and works identically on Metal, WebGL2, and desktop GL. All of
/// the arithmetic it displays lives in FrameStats, which is headless and
/// tested; this class only formats and colours.
/// The run, flattened for display. Passing a snapshot rather than the Sim
/// keeps render/ from depending on game/, and keeps the HUD honest: it can
/// only show what someone deliberately handed it.
struct RunSnapshot {
    const char* phase = "?";
    const char* reload = "?";
    i32 wave = 0;
    f32 phase_seconds_left = 0.0f;
    i32 enemies = 0;
    i32 salvage = 0;
    f32 health = 0.0f;
    f32 health_max = 1.0f;
    i32 ammo = 0;
    i32 mag_size = 0;
    bool boosted = false;
};

class DebugHud {
public:
    void toggle() { visible_ = !visible_; }

    void set_visible(bool visible) { visible_ = visible; }

    [[nodiscard]] bool visible() const { return visible_; }

    /// Formats and submits the overlay. Call once per frame after
    /// Renderer::render and before bgfx::frame. `now` is monotonic seconds,
    /// used only to throttle memory sampling.
    void draw(const FrameStats& frame,
              const RendererStats& render,
              const RunSnapshot& run,
              f64 now,
              const vec3& player);

    /// Memory is sampled at this interval rather than every frame: on Linux
    /// the query parses procfs, and a figure that only moves in megabytes does
    /// not need 60 updates a second.
    static constexpr f64 kMemorySampleSeconds = 0.25;

private:
    bool visible_ = false;
    MemoryUsage memory_;
    f64 next_memory_sample_ = 0.0;
};

}  // namespace df
