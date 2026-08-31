// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

namespace df::budget {

/// The performance budget from the README, in code so that something can
/// actually check it.
///
/// These are targets, not aspirations. The debug HUD colours every figure
/// against the matching constant here, which is the point: a budget nobody
/// measures is a wish. Phones do not fail at low frame rates — they fail at
/// *falling* frame rates twelve minutes in, when the SoC throttles, and the
/// only way to see that coming is to have the numbers on screen while playing.

/// 60 fps is the floor for a shooter. One frame at 60 Hz.
constexpr f64 kFrameMs = 1000.0 / 60.0;

/// Remeshing time per frame. One shotgun blast dirties four chunks; rebuilding
/// them all in the frame they were dirtied is a guaranteed hitch.
constexpr f64 kRemeshMs = 2.5;

/// Chunks rebuilt per frame. Mirrors Renderer::kRemeshBudgetPerFrame.
constexpr u32 kRemeshChunks = 4;

/// Mobile tilers stall on state changes, and every chunk is one draw.
constexpr u32 kDrawCalls = 300;

/// iOS jetsams above this on older devices.
constexpr u64 kResidentBytes = 700ull * 1024ull * 1024ull;

/// Fraction of a budget above which a figure is "close" rather than "fine".
/// The HUD turns amber here so that drift is visible before it is a breach.
constexpr f64 kWarnFraction = 0.85;

}  // namespace df::budget
