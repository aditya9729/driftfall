// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "render/debug_hud.hpp"

#include "core/perf_budget.hpp"

#include <bgfx/bgfx.h>

namespace df {
namespace {

// bgfx debug-text attributes are (background << 4) | foreground over the
// standard 16-colour palette.
constexpr u8 kLabel = 0x0f;  // white
constexpr u8 kDim = 0x08;    // dark grey
constexpr u8 kOk = 0x0a;     // light green
constexpr u8 kWarn = 0x0e;   // yellow
constexpr u8 kOver = 0x0c;   // light red

/// Green inside budget, amber in the last 15 % of it, red over. The amber band
/// is the useful one: it is where thermal drift becomes visible before it
/// becomes a dropped frame.
u8 attr_for(f64 value, f64 budget) {
    if (budget <= 0.0) return kLabel;
    if (value > budget) return kOver;
    if (value > budget * budget::kWarnFraction) return kWarn;
    return kOk;
}

constexpr f64 kBytesPerMb = 1024.0 * 1024.0;

}  // namespace

void DebugHud::draw(const FrameStats& frame, const RendererStats& render, f64 now, const vec3& player) {
    bgfx::setDebug(visible_ ? BGFX_DEBUG_TEXT : BGFX_DEBUG_NONE);
    if (!visible_) return;

    if (now >= next_memory_sample_) {
        memory_ = query_memory_usage();
        next_memory_sample_ = now + kMemorySampleSeconds;
    }

    bgfx::dbgTextClear();

    u16 row = 1;
    bgfx::dbgTextPrintf(2, row++, kLabel, "DRIFTFALL  debug  (F3 / two-finger tap, move half)");
    ++row;

    // --- frame time --------------------------------------------------------
    const f64 mean = frame.mean_ms();
    bgfx::dbgTextPrintf(2,
                        row++,
                        attr_for(mean, budget::kFrameMs),
                        "frame   %6.2f ms   %5.1f fps   (budget %.2f ms)",
                        mean,
                        frame.fps(),
                        budget::kFrameMs);

    const f64 p95 = frame.percentile_ms(0.95);
    bgfx::dbgTextPrintf(2,
                        row++,
                        attr_for(p95, budget::kFrameMs),
                        "  p95   %6.2f ms   max %6.2f   worst %6.2f",
                        p95,
                        frame.max_ms(),
                        frame.worst_ms());

    // --- remeshing ---------------------------------------------------------
    bgfx::dbgTextPrintf(2,
                        row++,
                        attr_for(render.remesh_ms, budget::kRemeshMs),
                        "remesh  %6.2f ms   %u chunk(s)   backlog %u",
                        render.remesh_ms,
                        static_cast<unsigned>(render.chunks_remeshed_this_frame),
                        static_cast<unsigned>(render.dirty_backlog));

    // --- draw submission ---------------------------------------------------
    bgfx::dbgTextPrintf(2,
                        row++,
                        attr_for(static_cast<f64>(render.draw_calls), static_cast<f64>(budget::kDrawCalls)),
                        "draws   %6u / %u   chunks %u   tris %uk",
                        static_cast<unsigned>(render.draw_calls),
                        static_cast<unsigned>(budget::kDrawCalls),
                        static_cast<unsigned>(render.chunks_drawn),
                        static_cast<unsigned>(render.triangles / 1000));

    // --- memory ------------------------------------------------------------
    if (memory_.valid) {
        const f64 resident_mb = static_cast<f64>(memory_.resident_bytes) / kBytesPerMb;
        const f64 budget_mb = static_cast<f64>(budget::kResidentBytes) / kBytesPerMb;
        bgfx::dbgTextPrintf(2,
                            row++,
                            attr_for(resident_mb, budget_mb),
                            "mem     %6.1f / %.0f MB resident",
                            resident_mb,
                            budget_mb);
    } else {
        bgfx::dbgTextPrintf(2, row++, kDim, "mem     unavailable on this platform");
    }

    ++row;

    // --- context -----------------------------------------------------------
    bgfx::dbgTextPrintf(2,
                        row++,
                        kDim,
                        "pos     %7.1f %7.1f %7.1f",
                        static_cast<f64>(player.x),
                        static_cast<f64>(player.y),
                        static_cast<f64>(player.z));

    // A dropped step means the device could not keep up and the simulation
    // deliberately fell behind wall-clock rather than spiral. A stall means the
    // app was not running at all. They look identical from the outside and mean
    // completely different things, so they are never merged into one counter.
    const u8 dropped_attr = frame.dropped_step_frames() > 0 ? kWarn : kDim;
    bgfx::dbgTextPrintf(2,
                        row++,
                        dropped_attr,
                        "sim     dropped %llu   stalls %llu   frames %llu",
                        static_cast<unsigned long long>(frame.dropped_step_frames()),
                        static_cast<unsigned long long>(frame.stall_count()),
                        static_cast<unsigned long long>(frame.total_frames()));
}

}  // namespace df
