// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "render/hud.hpp"

#include "core/log.hpp"
#include "generated/shaders/fs_hud.sc.bin.h"
#include "generated/shaders/vs_hud.sc.bin.h"

#include <bgfx/embedded_shader.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace df {
namespace {

const bgfx::EmbeddedShader kEmbeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_hud),
    BGFX_EMBEDDED_SHADER(fs_hud),
    BGFX_EMBEDDED_SHADER_END(),
};

// --- colour ----------------------------------------------------------------
// These are *display-space* values, not linear ones, and that is deliberate.
// The scene shaders each tonemap and encode to sRGB themselves (see the last
// two lines of fs_chunk.sc); by the time this view runs, the backbuffer already
// holds finished display-space pixels. So the HUD composites over that in
// display space and its fragment shader is a straight passthrough. Running it
// through ACES instead would wash every readout out — a bar authored at 0.85
// comes back at about 0.7 and the "danger red" turns salmon — and it would also
// mean the HUD's legibility changed whenever the exposure was retuned, which is
// exactly the coupling a HUD must not have.

constexpr u32 rgba(u8 r, u8 g, u8 b, u8 a) {
    // bgfx reads a normalised Uint8x4 attribute as R,G,B,A in memory order, so
    // on a little-endian target that is 0xAABBGGRR packed.
    return (static_cast<u32>(a) << 24) | (static_cast<u32>(b) << 16) | (static_cast<u32>(g) << 8) |
           static_cast<u32>(r);
}

/// Ink for the plates behind clusters. Not black: pure black over a near-black
/// derelict reads as a hole punched in the frame.
constexpr u32 kInk = rgba(8, 10, 16, 170);
constexpr u32 kTrack = rgba(28, 33, 42, 235);
constexpr u32 kEdge = rgba(96, 108, 126, 230);
constexpr u32 kLabel = rgba(128, 142, 162, 255);
constexpr u32 kDim = rgba(78, 88, 102, 255);
constexpr u32 kValue = rgba(206, 216, 230, 255);

/// Hull. Cold green through amber to red — the only three-stop ramp in the
/// game, because it is the only readout the player reads while being shot at
/// and colour is faster to parse than length.
constexpr u32 kHullFull = rgba(92, 208, 158, 255);
constexpr u32 kHullWarn = rgba(226, 186, 84, 255);
constexpr u32 kHullLow = rgba(226, 78, 62, 255);

/// The chip bar: what you just lost, held for a beat before it drains away.
constexpr u32 kChip = rgba(236, 214, 176, 200);

/// Salvage takes the ore material's colour out of the palette in renderer.cpp,
/// so the currency on screen matches the thing you mined for it.
constexpr u32 kSalvage = rgba(198, 166, 90, 255);

/// The active-reload window. Amber for perfect, cold blue for good: the two
/// bands must never be told apart by brightness alone, because the moment the
/// player is judging them the screen is also full of muzzle flash.
constexpr u32 kPerfect = rgba(255, 206, 104, 255);
constexpr u32 kGood = rgba(96, 162, 214, 235);
constexpr u32 kSpent = rgba(70, 78, 92, 235);
constexpr u32 kProgress = rgba(58, 74, 96, 255);
constexpr u32 kPlayhead = rgba(244, 250, 255, 255);
constexpr u32 kJam = rgba(226, 78, 62, 255);

constexpr u32 kCrosshair = rgba(226, 236, 246, 235);
constexpr u32 kCrosshairEdge = rgba(6, 8, 12, 190);

u32 with_alpha(u32 color, f32 scale) {
    const f32 a = static_cast<f32>((color >> 24) & 0xffu) * std::clamp(scale, 0.0f, 1.0f);
    return (color & 0x00ffffffu) | (static_cast<u32>(a) << 24);
}

u32 mix_color(u32 a, u32 b, f32 t) {
    const f32 k = std::clamp(t, 0.0f, 1.0f);
    u32 out = 0;
    for (u32 shift = 0; shift < 32; shift += 8) {
        const f32 ca = static_cast<f32>((a >> shift) & 0xffu);
        const f32 cb = static_cast<f32>((b >> shift) & 0xffu);
        out |= static_cast<u32>(ca + (cb - ca) * k) << shift;
    }
    return out;
}

// --- font ------------------------------------------------------------------
// A 5x7 uppercase bitmap font, one u8 row mask per row, bit 0x10 leftmost.
//
// Only the glyphs a HUD actually needs exist. That is the whole argument for
// doing it this way rather than pulling in a font library or baking an atlas:
// forty-four glyphs of blocky numerals is a table you can read, it scales with
// the virtual-pixel canvas instead of with device pixels, it tints per element,
// and it costs no asset-loading path — which the web build, built with no
// filesystem of its own for our data, does not have.

constexpr u32 kGlyphCols = 5;
constexpr u32 kGlyphRows = 7;
constexpr char kGlyphOrder[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:/-+%!";
constexpr usize kGlyphCount = sizeof(kGlyphOrder) - 1;

constexpr u8 kFont[kGlyphCount][kGlyphRows] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // space
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
    {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E},  // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},  // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},  // 9
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // A
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},  // B
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},  // C
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},  // D
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},  // E
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},  // F
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},  // G
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // H
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},  // I
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},  // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},  // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},  // L
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},  // M
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},  // N
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // O
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},  // P
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},  // Q
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},  // R
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},  // S
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},  // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // U
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},  // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},  // W
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},  // X
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},  // Y
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},  // Z
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04},  // .
    {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00},  // :
    {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10},  // /
    {0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00},  // -
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},  // +
    {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13},  // %
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},  // !
};

/// Linear scan of a 44-character table. A switch or a 96-entry sparse array
/// would both be faster and neither is worth it: the HUD draws on the order of
/// thirty glyphs a frame, so this is a few hundred byte comparisons against a
/// string that is certainly in L1.
usize glyph_index(char c) {
    char upper = c;
    if (upper >= 'a' && upper <= 'z') upper = static_cast<char>(upper - ('a' - 'A'));
    for (usize i = 0; i < kGlyphCount; ++i) {
        if (kGlyphOrder[i] == upper) return i;
    }
    return 0;  // anything unmapped is a space rather than a missing-glyph box
}

bool same_text(const char* a, const char* b) {
    return a != nullptr && b != nullptr && std::strcmp(a, b) == 0;
}

// --- layout ----------------------------------------------------------------
// All in virtual pixels on the kVirtualHeight-tall canvas.

constexpr f32 kMargin = 22.0f;
constexpr f32 kHealthWidth = 300.0f;
constexpr f32 kHealthHeight = 18.0f;
constexpr f32 kReloadWidth = 340.0f;
constexpr f32 kReloadHeight = 14.0f;

/// How far below the crosshair the reload bar sits. Close enough that it is in
/// the same glance as the enemy you are shooting — the tap is judged inside a
/// window about 170 ms wide on the rifle, which is reflex range, not
/// look-down-at-the-corner range — but far enough that it never sits on top of
/// what you are aiming at.
constexpr f32 kReloadDrop = 52.0f;

constexpr f32 kChipHoldSeconds = 0.35f;
constexpr f32 kChipDrainRate = 6.0f;  ///< exponential rate, per second
constexpr f32 kCueFlashSeconds = 0.55f;

}  // namespace

PlayerHud::PlayerHud() = default;

PlayerHud::~PlayerHud() {
    shutdown();
}

bool PlayerHud::initialise() {
    if (initialised_) return true;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    program_ = bgfx::createProgram(bgfx::createEmbeddedShader(kEmbeddedShaders, type, "vs_hud"),
                                   bgfx::createEmbeddedShader(kEmbeddedShaders, type, "fs_hud"),
                                   true);
    if (!bgfx::isValid(program_)) {
        log::error("failed to create the HUD shader program");
        return false;
    }

    // See HudVertex: the colour attribute is normalised because an integer
    // vertex attribute is a silent, desktop-invisible failure on WebGL2.
    layout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, false)
        .end();
    // The frame's vertices are memcpy'd into the transient buffer as a block,
    // so any padding the compiler inserted would shear the whole overlay.
    static_assert(sizeof(HudVertex) == 12, "vertex layout and HudVertex must agree");

    // Enough for a busy frame — health, salvage, wave, the reload bar and about
    // thirty glyphs — so the vector never grows during play.
    vertices_.reserve(4096);

    initialised_ = true;
    return true;
}

void PlayerHud::shutdown() {
    if (!initialised_) return;
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;
    initialised_ = false;
}

void PlayerHud::resize(i32 width, i32 height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
}

vec2 PlayerHud::virtual_size() const {
    const f32 aspect = static_cast<f32>(width_) / static_cast<f32>(height_);
    f32 height = kVirtualHeight;
    f32 width = height * aspect;
    if (width < kMinVirtualWidth) {
        width = kMinVirtualWidth;
        height = width / aspect;
    }
    return vec2{width, height};
}

void PlayerHud::quad(f32 x, f32 y, f32 w, f32 h, u32 color) {
    // Degenerate quads are not free — they still consume six vertices and reach
    // the rasteriser — and they turn up constantly here, from empty bars and
    // from windows whose two bounds are tuned to the same value.
    if (w <= 0.0f || h <= 0.0f) return;

    const f32 x1 = x + w;
    const f32 y1 = y + h;
    vertices_.push_back(HudVertex{x, y, color});
    vertices_.push_back(HudVertex{x1, y, color});
    vertices_.push_back(HudVertex{x1, y1, color});
    vertices_.push_back(HudVertex{x, y, color});
    vertices_.push_back(HudVertex{x1, y1, color});
    vertices_.push_back(HudVertex{x, y1, color});
}

void PlayerHud::outline(f32 x, f32 y, f32 w, f32 h, f32 t, u32 color) {
    quad(x, y, w, t, color);
    quad(x, y + h - t, w, t, color);
    quad(x, y + t, t, h - t * 2.0f, color);
    quad(x + w - t, y + t, t, h - t * 2.0f, color);
}

void PlayerHud::plate(f32 x, f32 y, f32 w, f32 h) {
    quad(x, y, w, h, kInk);
}

f32 PlayerHud::text_width(const char* str, f32 px) {
    if (str == nullptr || *str == '\0') return 0.0f;
    usize count = 0;
    for (const char* c = str; *c != '\0'; ++c) ++count;
    // The trailing advance gap is not part of the width, or right-aligned text
    // would always sit one font pixel short of its anchor.
    return (static_cast<f32>(count) * static_cast<f32>(kGlyphCols + 1) - 1.0f) * px;
}

void PlayerHud::text(f32 x, f32 y, f32 px, const char* str, u32 color) {
    if (str == nullptr) return;

    f32 pen = x;
    for (const char* c = str; *c != '\0'; ++c) {
        const u8* glyph = kFont[glyph_index(*c)];
        for (u32 row = 0; row < kGlyphRows; ++row) {
            const u8 bits = glyph[row];
            u32 col = 0;
            while (col < kGlyphCols) {
                if ((bits & (0x10u >> col)) == 0u) {
                    ++col;
                    continue;
                }
                // Merge each horizontal run of set pixels into one quad. It is
                // the greedy mesher's trick in one dimension, and it takes a
                // glyph from 35 quads to about 10 — which is the difference
                // between the font being free and the font being the most
                // expensive thing in the overlay.
                u32 run = 1;
                while (col + run < kGlyphCols && (bits & (0x10u >> (col + run))) != 0u) ++run;
                quad(pen + static_cast<f32>(col) * px,
                     y + static_cast<f32>(row) * px,
                     static_cast<f32>(run) * px,
                     px,
                     color);
                col += run;
            }
        }
        pen += static_cast<f32>(kGlyphCols + 1) * px;
    }
}

void PlayerHud::update_animation(const RunSnapshot& run, const WeaponHudView& weapon, f32 dt) {
    time_ += dt;

    // --- chip health -------------------------------------------------------
    if (chip_health_ < 0.0f) {
        // First frame. Seeding from the live value avoids a phantom drain
        // across the whole bar on the frame the HUD appears.
        chip_health_ = run.health;
        last_health_ = run.health;
    }

    // Damage is inferred from the value falling rather than from an event: the
    // snapshot carries state, not events, and that is the trade the snapshot
    // rule buys. A hit restarts the hold, so a burst that lands over several
    // frames leaves one chip bar rather than a staircase of them.
    if (run.health < last_health_) chip_hold_ = kChipHoldSeconds;
    last_health_ = run.health;

    if (run.health >= chip_health_) {
        chip_health_ = run.health;  // healing, or a fresh run: no lag on the way up
        chip_hold_ = 0.0f;
    } else if (chip_hold_ > 0.0f) {
        chip_hold_ = std::max(0.0f, chip_hold_ - dt);
    } else {
        // Exponential, expressed against dt so the drain looks the same at 30
        // and at 120 fps — the same reason Camera::update does it that way.
        const f32 k = 1.0f - std::exp(-kChipDrainRate * dt);
        chip_health_ += (run.health - chip_health_) * k;
        if (chip_health_ - run.health < 0.35f) chip_health_ = run.health;
    }

    // --- reload cue --------------------------------------------------------
    if (weapon.cue != HudReloadCue::None) {
        cue_ = weapon.cue;
        cue_flash_ = kCueFlashSeconds;
    }
    cue_flash_ = std::max(0.0f, cue_flash_ - dt);
    // The latch is cleared when the next reload starts, not when the flash ends:
    // between the tap and the end of a jammed reload the window is *spent*, and
    // the bar has to keep saying so.
    if (!weapon.reloading && cue_flash_ <= 0.0f) cue_ = HudReloadCue::None;
}

void PlayerHud::draw(const RunSnapshot& run, const WeaponHudView& weapon, f32 dt) {
    if (!initialised_ || !visible_) return;

    update_animation(run, weapon, dt);

    vertices_.clear();
    const vec2 canvas = virtual_size();

    draw_health(run, canvas);
    draw_salvage(run, canvas);
    draw_wave(run, canvas);
    draw_weapon(run, weapon, canvas);
    draw_crosshair(canvas);

    quads_last_frame_ = static_cast<u32>(vertices_.size() / 6);
    if (vertices_.empty()) return;

    const u32 count = static_cast<u32>(vertices_.size());
    if (bgfx::getAvailTransientVertexBuffer(count, layout_) < count) {
        // Dropping the overlay for one frame is the right failure: the
        // alternative is a half-drawn HUD, and a health bar that is sometimes
        // truncated is worse than one that briefly is not there.
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, count, layout_);
    std::memcpy(tvb.data, vertices_.data(), static_cast<usize>(count) * sizeof(HudVertex));

    // Clip-space depth differs per backend even for an orthographic matrix, and
    // getting it wrong here fails exactly the way camera.cpp describes: the
    // geometry is submitted, the draw-call count looks healthy, and nothing
    // survives clipping. Top-left origin, so `bottom` and `top` are swapped.
    const bool homogeneous_depth = bgfx::getCaps()->homogeneousDepth;
    const glm::mat4 proj = homogeneous_depth ? glm::orthoLH_NO(0.0f, canvas.x, canvas.y, 0.0f, 0.0f, 1.0f)
                                             : glm::orthoLH_ZO(0.0f, canvas.x, canvas.y, 0.0f, 0.0f, 1.0f);

    bgfx::setViewRect(kHudView, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
    bgfx::setViewTransform(kHudView, nullptr, &proj[0][0]);

    // No depth test, no depth write, no culling: this is a 2D pass composited
    // over a finished frame, and a quad's winding here is whatever the emit
    // order happened to give. Straight alpha, and the whole overlay is one
    // draw call, so blend order is simply the order quads were pushed.
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(kHudView, program_);
}

void PlayerHud::draw_health(const RunSnapshot& run, vec2 canvas) {
    const f32 x = kMargin;
    const f32 y = canvas.y - kMargin - kHealthHeight;
    const f32 max_health = run.health_max > 0.0f ? run.health_max : 1.0f;
    const f32 frac = std::clamp(run.health / max_health, 0.0f, 1.0f);
    const f32 chip_frac = std::clamp(chip_health_ / max_health, 0.0f, 1.0f);

    const u32 fill = frac >= 0.5f ? mix_color(kHullWarn, kHullFull, (frac - 0.5f) * 2.0f)
                                  : mix_color(kHullLow, kHullWarn, frac * 2.0f);

    plate(x - 10.0f, y - 34.0f, kHealthWidth + 20.0f, kHealthHeight + 44.0f);

    text(x, y - 28.0f, 2.0f, "HULL", kLabel);

    char buffer[16];
    // Ceil, not round: 0.4 hull left has to read as 1, or the bar says you are
    // alive and the number says you are not.
    std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::ceil(run.health)));
    text(x + kHealthWidth - text_width(buffer, 3.0f), y - 32.0f, 3.0f, buffer, fill);

    quad(x, y, kHealthWidth, kHealthHeight, kTrack);
    if (chip_frac > frac) {
        quad(x + kHealthWidth * frac, y, kHealthWidth * (chip_frac - frac), kHealthHeight, kChip);
    }
    quad(x, y, kHealthWidth * frac, kHealthHeight, fill);

    // Quarter notches. Fractions are much harder to read off a bare bar than
    // people assume, and "under half" is a decision the player makes constantly.
    for (u32 i = 1; i < 4; ++i) {
        const f32 notch = x + kHealthWidth * (static_cast<f32>(i) * 0.25f);
        quad(notch - 1.0f, y, 2.0f, kHealthHeight, kInk);
    }

    outline(x, y, kHealthWidth, kHealthHeight, 1.0f, kEdge);

    // Below a quarter the bar grows a pulsing border. Motion is caught by
    // peripheral vision; a colour change alone is not, and at that point the
    // player is looking at the horde and not at the corner.
    if (frac < 0.25f) {
        const f32 pulse = 0.45f + 0.55f * (0.5f + 0.5f * std::sin(time_ * 9.0f));
        outline(
            x - 4.0f, y - 4.0f, kHealthWidth + 8.0f, kHealthHeight + 8.0f, 2.0f, with_alpha(kHullLow, pulse));
    }
}

void PlayerHud::draw_salvage(const RunSnapshot& run, vec2 canvas) {
    const f32 right = canvas.x - kMargin;
    const f32 y = canvas.y - kMargin - 21.0f;  // 3 px per font pixel * 7 rows

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(run.salvage));
    const f32 value_w = text_width(buffer, 3.0f);
    const f32 label_w = text_width("SALVAGE", 2.0f);
    const f32 block_w = std::max(value_w + 22.0f, label_w);

    plate(right - block_w - 10.0f, y - 34.0f, block_w + 20.0f, 65.0f);

    text(right - label_w, y - 28.0f, 2.0f, "SALVAGE", kLabel);
    text(right - value_w, y, 3.0f, buffer, kSalvage);

    // An ore chip, built out of scanlines because every quad here is
    // axis-aligned. It gives the currency an identity, so the bottom-right
    // corner is not two anonymous numbers stacked on each other.
    const f32 cx = right - value_w - 13.0f;
    const f32 cy = y + 10.0f;
    for (u32 i = 0; i < 5; ++i) {
        const f32 row = static_cast<f32>(i);
        const f32 half = 2.0f * (2.0f - std::fabs(row - 2.0f)) + 1.0f;
        quad(cx - half, cy - 5.0f + row * 2.0f, half * 2.0f, 2.0f, kSalvage);
    }
}

void PlayerHud::draw_wave(const RunSnapshot& run, vec2 canvas) {
    const f32 centre = canvas.x * 0.5f;

    char wave_text[24];
    std::snprintf(wave_text, sizeof(wave_text), "WAVE %d", static_cast<int>(run.wave));
    const f32 wave_w = text_width(wave_text, 4.0f);
    const f32 phase_w = text_width(run.phase, 2.0f);

    // The snapshot carries the time *remaining* but not the phase length, and
    // the length is wave-dependent (75 s at wave 1, floored at 22 by wave 12).
    // Rather than hardcode the design document's curve here — which would
    // silently drift the moment the director is retuned — the span is taken
    // from the timer itself: a countdown that jumps upwards is a new phase
    // starting, and that peak is its length. Self-correcting within one frame,
    // and tracked in every phase so entering Prep already has the answer.
    if (run.phase_seconds_left > last_phase_seconds_ + 0.001f) phase_span_ = run.phase_seconds_left;
    last_phase_seconds_ = run.phase_seconds_left;

    const bool prep = same_text(run.phase, "PREP");
    const f32 block_h = prep ? 88.0f : 60.0f;
    const f32 block_w = std::max({wave_w, phase_w, kHealthWidth * 0.6f}) + 24.0f;

    plate(centre - block_w * 0.5f, kMargin - 8.0f, block_w, block_h);

    text(centre - wave_w * 0.5f, kMargin, 4.0f, wave_text, kValue);
    text(centre - phase_w * 0.5f, kMargin + 34.0f, 2.0f, run.phase, prep ? kSalvage : kLabel);

    if (!prep) return;

    // --- the prep countdown ------------------------------------------------
    const f32 span = phase_span_ > 0.0f ? phase_span_ : 1.0f;
    const f32 left = std::clamp(run.phase_seconds_left / span, 0.0f, 1.0f);

    const f32 bar_w = block_w - 40.0f;
    const f32 bar_x = centre - bar_w * 0.5f;
    const f32 bar_y = kMargin + 58.0f;

    quad(bar_x, bar_y, bar_w, 6.0f, kTrack);
    // Draining right-to-left: the bar empties towards the side it started from,
    // so "nearly out of time" is a short bar rather than a long one.
    quad(bar_x, bar_y, bar_w * left, 6.0f, left < 0.2f ? kHullLow : kSalvage);
    outline(bar_x, bar_y, bar_w, 6.0f, 1.0f, kEdge);

    char seconds[16];
    // Ceil again, so the last second is displayed as 1 for its full duration
    // rather than as 0 while it is still running.
    std::snprintf(seconds, sizeof(seconds), "%d", static_cast<int>(std::ceil(run.phase_seconds_left)));
    // Whole seconds only. Tenths on a build timer flicker at 60 Hz and read as
    // noise; the drain bar carries the sub-second feel.
    text(centre - text_width(seconds, 3.0f) * 0.5f,
         bar_y + 12.0f,
         3.0f,
         seconds,
         left < 0.2f ? kHullLow : kValue);
}

void PlayerHud::draw_weapon(const RunSnapshot& run, const WeaponHudView& weapon, vec2 canvas) {
    // --- the counters, in the periphery ------------------------------------
    // Ammo sits above salvage in the right-hand stack. Both are numbers you
    // check *between* fights, which is exactly why neither belongs in the
    // centre where the reload bar goes.
    const f32 right = canvas.x - kMargin;
    // Stacked directly on top of the salvage plate rather than at some measured
    // offset from the bottom edge: two ink plates that overlap darken where they
    // cross, and that seam reads as a rendering bug rather than as a panel.
    const f32 salvage_top = canvas.y - kMargin - 21.0f - 34.0f;
    const f32 y = salvage_top - 6.0f - 35.0f;  // 5 px per font pixel, 7 rows tall

    char ammo_text[16];
    char mag_text[16];
    std::snprintf(ammo_text, sizeof(ammo_text), "%d", static_cast<int>(run.ammo));
    std::snprintf(mag_text, sizeof(mag_text), "/%d", static_cast<int>(run.mag_size));

    const f32 mag_w = text_width(mag_text, 2.0f);
    const f32 ammo_w = text_width(ammo_text, 5.0f);
    const u32 ammo_color = run.ammo == 0 ? kHullLow : (run.boosted ? kPerfect : kValue);

    const bool show_boost = run.boosted && weapon.damage_bonus > 1.001f;
    const f32 plate_top = show_boost ? y - 30.0f : y - 8.0f;
    plate(right - ammo_w - mag_w - 22.0f, plate_top, ammo_w + mag_w + 32.0f, salvage_top - plate_top);
    text(right - mag_w - ammo_w - 6.0f, y, 5.0f, ammo_text, ammo_color);
    // Baseline-aligned with the big number, not top-aligned, so the magazine
    // size reads as a denominator instead of as a second number.
    text(right - mag_w, y + 21.0f, 2.0f, mag_text, kDim);

    if (show_boost) {
        char boost_text[16];
        std::snprintf(boost_text,
                      sizeof(boost_text),
                      "+%d%%",
                      static_cast<int>(std::lround((weapon.damage_bonus - 1.0f) * 100.0f)));
        text(right - text_width(boost_text, 2.0f), y - 22.0f, 2.0f, boost_text, kPerfect);
    }

    // --- the active reload, in the centre -----------------------------------
    const bool show = weapon.reloading || cue_flash_ > 0.0f;
    if (!show) {
        // The one nag the HUD is allowed. No auto-reload exists by design, so an
        // empty magazine with no prompt is a player standing in a horde pressing
        // a trigger that does nothing.
        if (run.ammo == 0) {
            const f32 prompt_w = text_width("RELOAD", 3.0f);
            const f32 blink = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(time_ * 6.0f));
            text(canvas.x * 0.5f - prompt_w * 0.5f,
                 canvas.y * 0.5f + kReloadDrop,
                 3.0f,
                 "RELOAD",
                 with_alpha(kHullLow, blink));
        }
        return;
    }

    const f32 bar_x = canvas.x * 0.5f - kReloadWidth * 0.5f;
    const f32 bar_y = canvas.y * 0.5f + kReloadDrop;

    plate(bar_x - 8.0f, bar_y - 30.0f, kReloadWidth + 16.0f, kReloadHeight + 44.0f);

    quad(bar_x, bar_y, kReloadWidth, kReloadHeight, kTrack);
    quad(bar_x, bar_y, kReloadWidth * std::clamp(weapon.progress, 0.0f, 1.0f), kReloadHeight, kProgress);

    // The bands are drawn at fixed fractions of the bar, which is exactly what
    // tap_reload() judges against — and it is also why a jam *stretches* the
    // reload instead of restarting it: the window stays where the player
    // learned it was, and only the playhead slows down.
    const f32 perfect_start = std::clamp(weapon.perfect_start, 0.0f, 1.0f);
    const f32 perfect_end = std::clamp(weapon.perfect_end, perfect_start, 1.0f);
    const f32 good_end = std::clamp(weapon.good_end, perfect_end, 1.0f);

    // One judged tap per reload. Once it is spent the bands go grey: a bar that
    // still advertises a window the player can no longer hit is teaching them
    // the wrong thing.
    const bool spent = cue_ != HudReloadCue::None && weapon.reloading;
    const u32 good_color = spent ? kSpent : kGood;
    const u32 perfect_color = spent ? kSpent : kPerfect;

    quad(bar_x + kReloadWidth * perfect_end,
         bar_y,
         kReloadWidth * (good_end - perfect_end),
         kReloadHeight,
         good_color);

    // The perfect band overhangs the track top and bottom. Width alone cannot
    // carry it — on the mining lance it is 13 % of the bar and on the shotgun
    // 7 %, and at 7 % of 340 virtual pixels a flat band inside the track is a
    // smear you cannot aim at.
    quad(bar_x + kReloadWidth * perfect_start,
         bar_y - 5.0f,
         kReloadWidth * (perfect_end - perfect_start),
         kReloadHeight + 10.0f,
         perfect_color);

    // Hairlines at the two edges that actually decide the outcome, carried up
    // out of the bar. These are what the player learns: the shape above the
    // track, not the colour inside it.
    quad(bar_x + kReloadWidth * perfect_start - 1.0f, bar_y - 13.0f, 2.0f, 9.0f, perfect_color);
    quad(bar_x + kReloadWidth * perfect_end - 1.0f, bar_y - 13.0f, 2.0f, 9.0f, perfect_color);

    // --- playhead ----------------------------------------------------------
    const f32 head = bar_x + kReloadWidth * std::clamp(weapon.progress, 0.0f, 1.0f);

    // A short fading trail behind the head. At 60 Hz the head moves about 2.7
    // virtual pixels a frame on the rifle, which is far too little to read as
    // direction from a single hard line; the trail is what makes the motion —
    // and therefore the timing — legible instead of a jitter.
    for (u32 i = 1; i <= 4; ++i) {
        const f32 step = static_cast<f32>(i);
        const f32 trail_w = 7.0f;
        const f32 trail_x = head - step * trail_w;
        if (trail_x < bar_x) break;
        quad(trail_x, bar_y, trail_w, kReloadHeight, with_alpha(kPlayhead, 0.30f / step));
    }

    quad(head - 1.5f, bar_y - 7.0f, 3.0f, kReloadHeight + 14.0f, kPlayhead);

    outline(bar_x, bar_y, kReloadWidth, kReloadHeight, 1.0f, kEdge);

    // --- the verdict --------------------------------------------------------
    if (cue_flash_ <= 0.0f || cue_ == HudReloadCue::None) return;

    const f32 fade = std::clamp(cue_flash_ / kCueFlashSeconds, 0.0f, 1.0f);
    const char* word = cue_ == HudReloadCue::Perfect ? "PERFECT"
                       : cue_ == HudReloadCue::Good  ? "GOOD"
                                                     : "JAMMED";
    const u32 word_color = cue_ == HudReloadCue::Perfect ? kPerfect
                           : cue_ == HudReloadCue::Good  ? kGood
                                                         : kJam;

    // The word rises as it fades. A static label that blinks is easy to miss in
    // peripheral vision; movement is not, and this is feedback the player is
    // trying to learn a timing from.
    const f32 rise = (1.0f - fade) * 6.0f;
    const f32 word_w = text_width(word, 3.0f);
    text(canvas.x * 0.5f - word_w * 0.5f, bar_y - 26.0f - rise, 3.0f, word, with_alpha(word_color, fade));

    // A jam also flashes the whole track, because it is the one outcome that
    // costs the player something and it happens while they are looking at an
    // enemy rather than at the bar.
    if (cue_ == HudReloadCue::Jammed) {
        outline(bar_x - 3.0f,
                bar_y - 3.0f,
                kReloadWidth + 6.0f,
                kReloadHeight + 6.0f,
                2.0f,
                with_alpha(kJam, fade));
    }
}

void PlayerHud::draw_crosshair(vec2 canvas) {
    // Deliberately four ticks and a dot. The design is aim-assisted and
    // positional; a crosshair that grows, spreads and blooms would be
    // advertising a precision layer this game does not have.
    const f32 cx = std::floor(canvas.x * 0.5f);
    const f32 cy = std::floor(canvas.y * 0.5f);
    constexpr f32 kGap = 5.0f;
    constexpr f32 kArm = 7.0f;
    constexpr f32 kThick = 2.0f;

    struct Arm {
        f32 x, y, w, h;
    };

    const Arm arms[4] = {
        {cx - kGap - kArm, cy - kThick * 0.5f, kArm, kThick},
        {cx + kGap, cy - kThick * 0.5f, kArm, kThick},
        {cx - kThick * 0.5f, cy - kGap - kArm, kThick, kArm},
        {cx - kThick * 0.5f, cy + kGap, kThick, kArm},
    };

    // Every arm gets a one-pixel dark border first. Without it the crosshair
    // disappears against ice and against muzzle flash — the two things it is
    // most often in front of.
    for (const Arm& arm : arms) {
        quad(arm.x - 1.0f, arm.y - 1.0f, arm.w + 2.0f, arm.h + 2.0f, kCrosshairEdge);
    }
    for (const Arm& arm : arms) {
        quad(arm.x, arm.y, arm.w, arm.h, kCrosshair);
    }

    quad(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f, kCrosshairEdge);
    quad(cx - 1.0f, cy - 1.0f, 2.0f, 2.0f, kCrosshair);
}

}  // namespace df
