# Copyright 2026 Aditya Gudal
# SPDX-License-Identifier: Apache-2.0
#
# Every dependency here is permissively licensed and compatible with
# redistributing DRIFTFALL under Apache-2.0. See NOTICE. Pin every tag: a
# floating dependency is a build that breaks on someone else's machine.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- glm (MIT) — header-only math -------------------------------------------
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# --- EnTT (MIT) — entity component system -----------------------------------
FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(EnTT)

# --- doctest (MIT) — unit tests ---------------------------------------------
if(DRIFTFALL_BUILD_TESTS)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(doctest)
endif()

# --- Client-only dependencies -----------------------------------------------
if(DRIFTFALL_BUILD_CLIENT)
    # SDL3 (zlib) — window, input, audio, iOS + Emscripten entry points.
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.8
        GIT_SHALLOW    TRUE
    )
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)

    # bgfx (BSD-2) — one shader pipeline that reaches Metal on iOS and
    # WebGL2/WebGPU in the browser. This is the whole reason the renderer is
    # not hand-written Metal.
    FetchContent_Declare(bgfx
        GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
        GIT_TAG        v1.127.8710-464
        GIT_SHALLOW    TRUE
    )
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
    set(BGFX_CONFIG_MULTITHREADED ON CACHE BOOL "" FORCE)
    # shaderc must be a host-native binary even when cross-compiling to wasm/iOS.
    set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(bgfx)

    include(Shaders)
endif()
