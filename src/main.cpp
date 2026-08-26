// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/log.hpp"
#include "platform/app.hpp"

#include <cstdlib>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace {

df::App g_app;

void frame_callback() {
    if (!g_app.frame()) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
    }
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    df::log::set_level(df::log::Level::Info);

    if (!g_app.initialise("DRIFTFALL", 1280, 720)) {
        df::log::error("startup failed");
        return EXIT_FAILURE;
    }

#if defined(__EMSCRIPTEN__)
    // The browser owns the frame loop. Passing 0 for fps means "use
    // requestAnimationFrame", which is the only way to match the display and
    // avoid burning battery on a phone.
    emscripten_set_main_loop(frame_callback, 0, 1);
#else
    while (g_app.running()) {
        frame_callback();
    }
    g_app.shutdown();
#endif

    return EXIT_SUCCESS;
}
