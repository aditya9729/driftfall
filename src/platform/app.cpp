// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "platform/app.hpp"

#include "core/log.hpp"
#include "voxel/raycast.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <algorithm>
#include <cmath>

namespace df {
namespace {

/// Fills in the native handles bgfx needs from an SDL3 window.
///
/// These must end up in bgfx::Init::platformData, not merely in a
/// setPlatformData() call before init. bgfx decides whether it is running
/// headless purely by looking at Init::platformData, and a headless device
/// with a non-zero backbuffer resolution is a hard init failure:
///
///     m_headless = ... && NULL == _init.platformData.nwh && ...;
///     if (m_headless && 0 != _init.resolution.width ...) return false;
///
/// It then overwrites the global platform data with Init::platformData
/// regardless, so a pre-init setPlatformData() is not just insufficient, it is
/// discarded. This is why every real backend failed while Noop succeeded —
/// Noop is the one renderer type excluded from that headless test.
bool fill_platform_data(SDL_Window* window, bgfx::PlatformData& pd) {
#if defined(__EMSCRIPTEN__)
    (void)window;
    // bgfx addresses the canvas by CSS selector on the web.
    pd.nwh = const_cast<void*>(static_cast<const void*>("#canvas"));
#else
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (props == 0) {
        log::error("SDL_GetWindowProperties failed: {}", SDL_GetError());
        return false;
    }

#if defined(SDL_PLATFORM_WIN32)
    pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_MACOS)
    pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_IOS)
    pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_LINUX)
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        pd.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        pd.type = bgfx::NativeWindowHandleType::Wayland;
    } else {
        pd.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        pd.nwh = reinterpret_cast<void*>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    }
#endif

    if (pd.nwh == nullptr) {
        log::error("could not obtain a native window handle from SDL");
        return false;
    }
#endif

    return true;
}

/// Player movement speed in voxels per second. A voxel is ~0.4 m, so this is a
/// deliberate, heavy jog — the Gears weight, not the Quake sprint.
constexpr f32 kMoveSpeed = 11.0f;

/// How far a shot reaches, in voxels. Beyond this the ray stops looking, which
/// bounds the walk and keeps a shot into open space from marching the length of
/// the sector.
constexpr f32 kWeaponRange = 96.0f;

const char* phase_name(Phase phase) {
    switch (phase) {
        case Phase::Prep:
            return "PREP";
        case Phase::Assault:
            return "ASSAULT";
        case Phase::Cleared:
            return "CLEARED";
        case Phase::Defeat:
            return "DEFEAT";
    }
    return "?";
}

const char* reload_name(ReloadState state) {
    switch (state) {
        case ReloadState::Ready:
            return "ready";
        case ReloadState::Reloading:
            return "reloading";
        case ReloadState::Jammed:
            return "JAMMED";
    }
    return "?";
}

}  // namespace

App::App() = default;

App::~App() {
    shutdown();
}

bool App::initialise(const char* title, i32 width, i32 height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        log::error("SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window_ == nullptr) {
        log::error("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    bgfx::PlatformData platform_data{};
    if (!fill_platform_data(window_, platform_data)) return false;

    // Calling renderFrame() *before* init() is how bgfx is told to run
    // single-threaded — setting init.callback does not do it, which is what
    // the comment here used to claim. A render thread needs SharedArrayBuffer
    // on the web, and on a phone it competes with the chunk mesher for the
    // same handful of cores, so we do not want one.
    bgfx::renderFrame();

    bgfx::Init init;
    // Auto lets bgfx pick Metal on Apple platforms and WebGL2 in the browser.
    init.type = bgfx::RendererType::Count;
    init.platformData = platform_data;
    init.resolution.width = static_cast<u32>(width);
    init.resolution.height = static_cast<u32>(height);
    init.resolution.reset = BGFX_RESET_VSYNC;
    init.callback = nullptr;

    if (!bgfx::init(init)) {
        log::error("bgfx::init failed");
        return false;
    }

    width_ = width;
    height_ = height;

    bgfx::setViewClear(Renderer::kMainView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x090b10ff, 1.0f, 0);
    bgfx::setViewRect(Renderer::kMainView, 0, 0, static_cast<u16>(width), static_cast<u16>(height));

    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->initialise()) return false;
    renderer_->resize(width, height);

    SimConfig config;
    sim_ = std::make_unique<Sim>(config);

    // Drop the player in the middle of the sector, standing on the deck.
    const ivec3 extent = sim_->world().size_in_voxels();
    player_position_ = vec3{static_cast<f32>(extent.x) * 0.5f, 2.0f, static_cast<f32>(extent.z) * 0.5f};

    camera_.set_viewport(width, height);
    camera_.set_target(player_position_);
    input_.set_viewport(width, height);

    // Everything starts dirty, so this is the initial full mesh build.
    renderer_->collect_dirty(sim_->world());

    last_time_ = now_seconds();
    running_ = true;
    log::info("DRIFTFALL ready");
    return true;
}

void App::pump_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESIZED) {
            handle_resize();
            continue;
        }
        (void)input_.handle_event(event);
    }
    input_.poll_keyboard();
}

void App::handle_resize() {
    i32 width = 0;
    i32 height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width <= 0 || height <= 0) return;
    if (width == width_ && height == height_) return;

    width_ = width;
    height_ = height;
    bgfx::reset(static_cast<u32>(width), static_cast<u32>(height), BGFX_RESET_VSYNC);
    renderer_->resize(width, height);
    camera_.set_viewport(width, height);
    input_.set_viewport(width, height);
}

void App::apply_frame_input() {
    const InputState& in = input_.state();

    // Look is a *delta* accumulated over the frame, so it is applied once per
    // frame. Applying it inside the fixed-step loop multiplied look speed by
    // the number of steps, which made the camera whip round at exactly the
    // moment the device was struggling to keep up.
    camera_.add_yaw_pitch(in.look.x, in.look.y);

    if (in.hud_toggle_pressed) hud_.toggle();

    // Edges are consumed here for the same reason: the fixed-step loop runs
    // zero to five times off one InputState, and an edge read inside it fires
    // once per step. A single reload tap became begin_reload() followed by
    // tap_reload() at nearly zero elapsed time — an instant jam, and only on
    // slow frames. Reproduced at 11 fps, absent at 40.
    if (in.reload_pressed) {
        Weapon& weapon = sim_->weapon();
        if (weapon.state() == ReloadState::Ready) {
            weapon.begin_reload();
        } else {
            switch (weapon.tap_reload()) {
                case ReloadResult::Perfect:
                    log::info("PERFECT RELOAD");
                    break;
                case ReloadResult::Good:
                    log::info("good reload");
                    break;
                case ReloadResult::Jammed:
                    log::info("jammed");
                    break;
                case ReloadResult::Ignored:
                    break;
            }
        }
    }
}

void App::apply_step_input(f32 dt) {
    const InputState& in = input_.state();

    // Movement and firing are continuous, so these genuinely do belong inside
    // the fixed step: motion is a velocity, and the weapon's rate of fire is
    // gated by its own clock, which advances one step at a time.
    const vec3 motion = camera_.flat_forward() * in.move.y + camera_.flat_right() * in.move.x;
    player_position_ += motion * (kMoveSpeed * dt);

    // TODO(M2): collide against the voxel world. Until then the player is a
    // free-flying camera, which is exactly what is needed to inspect meshing.
    const ivec3 extent = sim_->world().size_in_voxels();
    player_position_.x = std::clamp(player_position_.x, 1.0f, static_cast<f32>(extent.x) - 1.0f);
    player_position_.z = std::clamp(player_position_.z, 1.0f, static_cast<f32>(extent.z) - 1.0f);

    if (in.firing) {
        Weapon& weapon = sim_->weapon();
        if (weapon.fire() > 0.0f) {
            // Aim down the camera's forward axis rather than the player's:
            // the crosshair sits at the centre of the screen, and in an
            // over-the-shoulder camera that is not where the player is facing.
            const VoxelHit hit =
                raycast_voxels(sim_->world(), camera_.eye(), camera_.forward(), kWeaponRange);
            if (hit) sim_->shoot_voxel(hit.voxel, weapon.stats().voxel_damage);
        }
    }
}

RunSnapshot App::run_snapshot() const {
    RunSnapshot run;
    if (sim_ == nullptr) return run;

    const Weapon& weapon = sim_->weapon();
    run.phase = phase_name(sim_->phase());
    run.reload = reload_name(weapon.state());
    run.wave = sim_->wave_index();
    run.phase_seconds_left = sim_->phase_time_remaining();
    run.enemies = sim_->enemies_remaining();
    run.salvage = sim_->salvage();
    run.health = sim_->player_health().current;
    run.health_max = sim_->player_health().max;
    run.ammo = weapon.ammo();
    run.mag_size = weapon.stats().mag_size;
    run.boosted = weapon.boosted();
    return run;
}

bool App::frame() {
    if (!running_) return false;

    const f64 current = now_seconds();
    const f64 delta = current - last_time_;
    last_time_ = current;

    input_.begin_frame(static_cast<f32>(delta));
    pump_events();

    if (input_.state().quit) {
        running_ = false;
        return false;
    }

    // Everything that is a per-frame quantity — look delta, input edges —
    // happens exactly once, here. Everything continuous happens per step.
    apply_frame_input();

    const int steps = timestep_.advance(delta);
    const auto step = static_cast<f32>(FixedTimestep::kStep);
    for (int i = 0; i < steps; ++i) {
        apply_step_input(step);
        sim_->tick(step);
    }

    frame_stats_.push(delta, timestep_.dropped_steps());

    if (timestep_.dropped_steps()) {
        log::warn("frame took {:.1f} ms; simulation dropped steps", delta * 1000.0);
    }

    camera_.set_target(player_position_);
    camera_.update(static_cast<f32>(delta));

    renderer_->collect_dirty(sim_->world());
    renderer_->render(sim_->world(), camera_);
    hud_.draw(frame_stats_, renderer_->stats(), run_snapshot(), current, player_position_);

    bgfx::frame();
    return true;
}

void App::shutdown() {
    if (renderer_) {
        renderer_->shutdown();
        renderer_.reset();
    }
    sim_.reset();

    if (window_ != nullptr) {
        bgfx::shutdown();
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
    }
    running_ = false;
}

}  // namespace df
