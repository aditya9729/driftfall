// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/clock.hpp"
#include "core/frame_stats.hpp"
#include "core/types.hpp"
#include "game/sim.hpp"
#include "platform/input.hpp"
#include "render/camera.hpp"
#include "render/debug_hud.hpp"
#include "render/renderer.hpp"

#include <memory>

struct SDL_Window;

namespace df {

/// Owns the window, the graphics device, and the frame loop.
///
/// Split into initialise / frame / shutdown rather than a `run()` because
/// Emscripten cannot block in a main loop — the browser owns the loop and
/// calls us back. Keeping frame() callable from outside is what lets the same
/// code drive both a native while-loop and emscripten_set_main_loop.
class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool initialise(const char* title, i32 width, i32 height);

    /// One frame: events, fixed-step simulation, render. Returns false when
    /// the app should exit.
    bool frame();

    void shutdown();

    [[nodiscard]] bool running() const { return running_; }

private:
    void pump_events();

    void apply_input(f32 dt);

    void handle_resize();

    [[nodiscard]] RunSnapshot run_snapshot() const;

    SDL_Window* window_ = nullptr;
    std::unique_ptr<Sim> sim_;
    std::unique_ptr<Renderer> renderer_;
    Camera camera_;
    Input input_;
    FixedTimestep timestep_;
    FrameStats frame_stats_;
    DebugHud hud_;
    vec3 player_position_{0.0f};
    f64 last_time_ = 0.0;
    i32 width_ = 0;
    i32 height_ = 0;
    bool running_ = false;
};

}  // namespace df
