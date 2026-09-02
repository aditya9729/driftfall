// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

#include <array>
#include <cmath>

namespace df::art {

/// The lighting model, in one place.
///
/// These were private to renderer.cpp, which was fine while the sector was the
/// only thing drawn. It stopped being fine the moment entities got their own
/// pass: entity_renderer.cpp could not reach them and copied every value
/// byte-for-byte, which is precisely the arrangement that drifts. Two lighting
/// models that agree today and are edited separately tomorrow is exactly how
/// enemies end up looking pasted onto the world — and the failure is gradual
/// and easy to explain away, which makes it worse.
///
/// Every value is *linear*. The shaders work in linear throughout and only the
/// final write is encoded back to display space, so mixing an authored display
/// value in here is what makes a flat-shaded surface look plastic.

/// Display-space to linear. The material palette is authored in display space
/// because that is the only way a human can read it; this is the one-way door
/// it passes through at startup.
///
/// Not constexpr: std::pow is not until C++26, and the alternative — a
/// hand-rolled constexpr approximation — would quietly disagree with the
/// shaders' own conversion.
[[nodiscard]] inline f32 srgb_to_linear(f32 c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

/// Cold starlight from above. w is the shared exposure, applied before the
/// tonemap by every shader that lights anything.
inline constexpr std::array<f32, 4> kAmbientSky = {0.050f, 0.072f, 0.120f, 1.0f};

/// Vacuum: essentially nothing bounces back off the deck. Keeping this near
/// black is what gives surfaces a direction instead of a uniform wash.
inline constexpr std::array<f32, 4> kAmbientGround = {0.012f, 0.013f, 0.018f, 0.0f};

/// A distant sun, slightly warm. w is intensity.
inline constexpr std::array<f32, 4> kKeyColor = {1.0f, 0.94f, 0.85f, 1.05f};

/// Rim light, cold. Picks the silhouette of cover out of the dark, which is
/// what makes the tactical layer readable at a glance.
///
/// Kept deliberately weak. Rim is an *additive* term, and a large flat surface
/// seen at a grazing angle has a fresnel of nearly 1 across its whole area — so
/// anything stronger stops being an edge highlight and becomes a blue wash over
/// the floor that buries both the material colour and the per-voxel grooves.
/// The entity pass deliberately scales this up, because a one-metre box does
/// not have the area that made a wall wash out; that weighting lives with the
/// entity shader, not here.
inline constexpr std::array<f32, 4> kRimColor = {0.16f, 0.28f, 0.50f, 0.10f};

/// The fog colour doubles as the sky horizon, so the two never disagree at the
/// point where a distant wall meets open space through a breach.
inline constexpr std::array<f32, 3> kFogColor = {0.020f, 0.028f, 0.048f};

inline constexpr std::array<f32, 4> kSkyZenith = {0.010f, 0.014f, 0.030f, 0.0f};
inline constexpr std::array<f32, 4> kSkyNadir = {0.004f, 0.005f, 0.008f, 0.0f};

/// Key light direction. Not normalised here on purpose — the shaders normalise,
/// and a value that looks like a direction but is scaled would be a trap.
inline constexpr std::array<f32, 4> kLightDir = {-0.42f, -0.78f, -0.46f, 0.0f};

/// x = distance at which fog starts, y = density. Exponential, so there is no
/// onset plane sweeping across the deck as you walk.
inline constexpr std::array<f32, 4> kFogParams = {18.0f, 0.016f, 0.0f, 0.0f};

/// The fog colour as a vec4 uniform. Spelled once because both passes need it
/// and both were building the same temporary.
[[nodiscard]] inline std::array<f32, 4> fog_color_rgba() {
    return {kFogColor[0], kFogColor[1], kFogColor[2], 1.0f};
}

}  // namespace df::art
