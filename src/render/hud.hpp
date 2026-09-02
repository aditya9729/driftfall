// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "render/debug_hud.hpp"  // RunSnapshot

#include <bgfx/bgfx.h>

#include <vector>

namespace df {

/// The result of the last judged reload tap, as far as the HUD needs to know.
///
/// A mirror of game/weapon.hpp's ReloadResult rather than the type itself:
/// render/ does not include game/, and duplicating four enumerators is a much
/// smaller price than the dependency. `Ignored` has no visual, so it is not
/// here — the caller maps it to `None`.
enum class HudReloadCue : u8 {
    None,
    Perfect,
    Good,
    Jammed,
};

/// The reload timing, flattened for display.
///
/// RunSnapshot deliberately carries only what fits in a debug line — the reload
/// *state* as a string, not its progress or its window bounds — so the active
/// reload cannot be drawn from it. Rather than reach into Weapon (which would
/// put game/ in render/'s include graph and break the rule debug_hud.hpp sets
/// out), the caller flattens the four numbers the bar needs. They come straight
/// off Weapon::reload_progress() and WeaponStats, so the bar is guaranteed to
/// be drawn against exactly the values tap_reload() judges the tap against —
/// which is the whole point. A bar that disagrees with the judgement by even a
/// few milliseconds teaches the player the wrong timing.
struct WeaponHudView {
    bool reloading = false;

    /// Weapon::reload_progress(), in [0,1].
    f32 progress = 0.0f;

    /// WeaponStats window bounds, as fractions of the reload. Defaults are the
    /// salvage rifle's, so a default-constructed view still draws something
    /// sane rather than a degenerate zero-width band.
    f32 perfect_start = 0.62f;
    f32 perfect_end = 0.70f;
    f32 good_end = 0.86f;

    /// WeaponStats::perfect_damage_bonus, as a multiplier. Shown next to the
    /// ammo count while a boosted magazine is loaded, and *not* hardcoded to
    /// "+30%": the mining lance's bonus is 1.0, and a HUD promising a bonus the
    /// weapon does not have is worse than no HUD at all.
    f32 damage_bonus = 1.30f;

    /// Set on the single frame a tap was judged; the HUD latches it and runs
    /// the flash itself. The caller does not have to hold a timer.
    HudReloadCue cue = HudReloadCue::None;
};

/// The player-facing HUD: hull, salvage, wave, weapon, crosshair.
///
/// Everything is one triangle list in one transient vertex buffer, submitted as
/// a single draw call in its own view. That is not premature optimisation — it
/// is what lets the HUD be free on a tiler, where a dozen small blended
/// full-width draws would each touch the same tile memory and the whole overlay
/// would show up as fragment cost in the frame the horde arrives.
///
/// Text is drawn from a built-in 5x7 blocky font rasterised as quads (see
/// hud.cpp), not from bgfx's debug text: debug text is fixed at 8x16 *device*
/// pixels, so it shrinks to illegibility on a 3x-density phone exactly where
/// this HUD has to be readable, and it cannot be tinted or positioned in the
/// virtual-pixel space everything else here lives in.
///
/// Like DebugHud, this consumes a flattened snapshot and never the Sim: it can
/// only show what someone deliberately handed it.
class PlayerHud {
public:
    PlayerHud();
    ~PlayerHud();

    PlayerHud(const PlayerHud&) = delete;
    PlayerHud& operator=(const PlayerHud&) = delete;

    /// Creates the program and vertex layout. Must be called after bgfx::init.
    bool initialise();

    void shutdown();

    /// Backbuffer size in *device* pixels. The virtual-pixel canvas is derived
    /// from it; see kVirtualHeight.
    void resize(i32 width, i32 height);

    void set_visible(bool visible) { visible_ = visible; }

    [[nodiscard]] bool visible() const { return visible_; }

    /// Builds and submits the overlay. Call once per frame after
    /// Renderer::render and before bgfx::frame. `dt` is the wall-clock frame
    /// delta, used only for the readouts that lag or pulse.
    void draw(const RunSnapshot& run, const WeaponHudView& weapon, f32 dt);

    /// Quads emitted last frame. Cheap to expose and the only way to notice the
    /// HUD quietly growing into a real cost.
    [[nodiscard]] u32 quad_count() const { return quads_last_frame_; }

    /// Its own view, after the sector (0) and the sky (1), so it composites
    /// over a finished frame. It must never be given a setViewClear.
    static constexpr bgfx::ViewId kHudView = 2;

    /// The HUD is laid out in virtual pixels on a canvas this many units tall,
    /// anchored to the real corners. Scaling by *height* is what keeps a health
    /// bar the same angular size on a phone and on a desktop browser; scaling
    /// by width would shrink the whole overlay every time someone widened a
    /// window.
    static constexpr f32 kVirtualHeight = 540.0f;

    /// ...except on a canvas narrower than 4:3, where a height-derived width
    /// gets too small to hold the bottom row and the corners start colliding.
    /// There the canvas is derived from the width instead and everything scales
    /// down together, which is ugly but never overlaps.
    static constexpr f32 kMinVirtualWidth = 720.0f;

private:
    /// Position in virtual pixels plus a packed colour.
    ///
    /// Float position and *normalised* uint8 colour, both deliberately. An
    /// unnormalised integer attribute is bound with glVertexAttribIPointer on
    /// WebGL2, the shader declares a_color0 as vec4, and every draw is rejected
    /// with GL_INVALID_OPERATION — visible only in the browser console, never
    /// on desktop. chunk_mesh.cpp carries the long version of this story.
    struct HudVertex {
        f32 x, y;
        u32 abgr;
    };

    [[nodiscard]] vec2 virtual_size() const;

    void quad(f32 x, f32 y, f32 w, f32 h, u32 color);

    /// A rectangle outline, `t` thick, drawn inside the given bounds.
    void outline(f32 x, f32 y, f32 w, f32 h, f32 t, u32 color);

    /// A dark plate behind a cluster. The scene underneath is a dark derelict
    /// most of the time and a muzzle flash the rest of it, and text with no
    /// plate is unreadable in the second case.
    void plate(f32 x, f32 y, f32 w, f32 h);

    /// `px` is the size of one font pixel, so a glyph is 5px wide and 7px tall.
    void text(f32 x, f32 y, f32 px, const char* str, u32 color);

    [[nodiscard]] static f32 text_width(const char* str, f32 px);

    void update_animation(const RunSnapshot& run, const WeaponHudView& weapon, f32 dt);

    void draw_health(const RunSnapshot& run, vec2 canvas);
    void draw_salvage(const RunSnapshot& run, vec2 canvas);
    void draw_wave(const RunSnapshot& run, vec2 canvas);
    void draw_weapon(const RunSnapshot& run, const WeaponHudView& weapon, vec2 canvas);
    void draw_crosshair(vec2 canvas);

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;

    /// Rebuilt every frame and copied into a transient buffer, but the vector
    /// itself is kept so the per-frame cost is a memcpy rather than a malloc.
    std::vector<HudVertex> vertices_;

    i32 width_ = 1280;
    i32 height_ = 720;
    bool initialised_ = false;
    bool visible_ = true;
    u32 quads_last_frame_ = 0;

    f32 time_ = 0.0f;

    /// The trailing "chip" health, in hull points. It falls towards the real
    /// value after a short hold, which is how the player sees *how much* a hit
    /// took off — a bar that snaps only ever shows where it ended up.
    f32 chip_health_ = -1.0f;
    f32 chip_hold_ = 0.0f;
    f32 last_health_ = 0.0f;

    /// The length of the phase currently counting down, recovered from the
    /// timer itself because the snapshot does not carry it. See draw_wave.
    f32 phase_span_ = 0.0f;
    f32 last_phase_seconds_ = 0.0f;

    HudReloadCue cue_ = HudReloadCue::None;
    f32 cue_flash_ = 0.0f;
};

}  // namespace df
