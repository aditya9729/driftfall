// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "platform/app.hpp"

#include "core/log.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <algorithm>
#include <cmath>

namespace df {
namespace {

/// Fills in the native handles bgfx needs from an SDL3 window.
bool set_platform_data(SDL_Window* window) {
    bgfx::PlatformData pd{};

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

    bgfx::setPlatformData(pd);
    return true;
}

/// Player movement speed in voxels per second. A voxel is ~0.4 m, so this is a
/// deliberate, heavy jog — the Gears weight, not the Quake sprint.
constexpr f32 kMoveSpeed = 11.0f;

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

    if (!set_platform_data(window_)) return false;

    bgfx::Init init;
    // Auto lets bgfx pick Metal on Apple platforms and WebGL2 in the browser.
    init.type = bgfx::RendererType::Count;
    init.resolution.width = static_cast<u32>(width);
    init.resolution.height = static_cast<u32>(height);
    init.resolution.reset = BGFX_RESET_VSYNC;
    // A single-threaded render submission path. bgfx's render thread is a win
    // on desktop, but on the web it requires SharedArrayBuffer and on a phone
    // it competes with the chunk mesher for the same limited cores.
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

void App::apply_input(f32 dt) {
    const InputState& in = input_.state();

    camera_.add_yaw_pitch(in.look.x, in.look.y);

    const vec3 motion = camera_.flat_forward() * in.move.y + camera_.flat_right() * in.move.x;
    player_position_ += motion * (kMoveSpeed * dt);

    // TODO(M2): collide against the voxel world. Until then the player is a
    // free-flying camera, which is exactly what is needed to inspect meshing.
    const ivec3 extent = sim_->world().size_in_voxels();
    player_position_.x = std::clamp(player_position_.x, 1.0f, static_cast<f32>(extent.x) - 1.0f);
    player_position_.z = std::clamp(player_position_.z, 1.0f, static_cast<f32>(extent.z) - 1.0f);

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

    if (in.firing) {
        const f32 damage = sim_->weapon().fire();
        if (damage > 0.0f) {
            // TODO(M2): raycast the voxel world from the camera and damage the
            // first solid voxel hit. The sim side of that already exists.
            (void)damage;
        }
    }
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

    const int steps = timestep_.advance(delta);
    const auto step = static_cast<f32>(FixedTimestep::kStep);
    for (int i = 0; i < steps; ++i) {
        apply_input(step);
        sim_->tick(step);
    }

    if (timestep_.dropped_steps()) {
        log::warn("frame took {:.1f} ms; simulation dropped steps", delta * 1000.0);
    }

    camera_.set_target(player_position_);
    camera_.update(static_cast<f32>(delta));

    renderer_->collect_dirty(sim_->world());
    renderer_->render(sim_->world(), camera_);

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
