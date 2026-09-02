# Copyright 2026 Aditya Gudal
# SPDX-License-Identifier: Apache-2.0
#
# Every dependency here is permissively licensed and compatible with
# redistributing DRIFTFALL under Apache-2.0. See NOTICE. Pin every tag: a
# floating dependency is a build that breaks on someone else's machine.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# CMake 4 removed compatibility with projects whose own cmake_minimum_required
# is below 3.5, and doctest v2.4.11 still declares one — so a runner that has
# moved to CMake 4 (macOS already has) fails at configure time on a dependency
# rather than on anything here. Their build files are not ours to edit and the
# pins are deliberate, so raise the floor on their behalf. CMake below 3.31 does
# not know this variable and simply ignores it.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Compile third-party headers as system headers.
#
# Our warning set is deliberately strict and applies to every translation unit,
# which means it also applies to whatever a dependency's headers happen to do.
# That is not a useful signal: EnTT declares `operator"" _hs` with a space,
# which newer clang deprecates, and with -Werror that fails the build over code
# nobody here can change. Marking their include directories SYSTEM keeps the
# strictness pointed at our own code, where it belongs.
function(driftfall_mark_dependency_system target)
    if(NOT TARGET ${target})
        return()
    endif()
    get_target_property(includes ${target} INTERFACE_INCLUDE_DIRECTORIES)
    if(includes)
        set_target_properties(${target} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${includes}")
    endif()
endfunction()

# --- glm (MIT) — header-only math -------------------------------------------
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)
driftfall_mark_dependency_system(glm)

# --- EnTT (MIT) — entity component system -----------------------------------
FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(EnTT)
driftfall_mark_dependency_system(EnTT)

# --- doctest (MIT) — unit tests ---------------------------------------------
if(DRIFTFALL_BUILD_TESTS)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(doctest)
    driftfall_mark_dependency_system(doctest)
endif()

# --- Client-only dependencies -----------------------------------------------
if(DRIFTFALL_BUILD_CLIENT OR DRIFTFALL_HOST_TOOLS_ONLY)
    # SDL3 is deliberately skipped for a host-tools configure. shaderc needs
    # bgfx and nothing else, whereas SDL3 refuses to configure on Linux without
    # X11 or Wayland development libraries — so pulling it in made building a
    # shader compiler depend on the runner having a desktop windowing stack.
    if(NOT DRIFTFALL_HOST_TOOLS_ONLY)
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
    endif()

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
    # bx hard-disables threading on Emscripten — BX_CONFIG_SUPPORTS_THREADING
    # is defined as !BX_PLATFORM_EMSCRIPTEN and no compiler flag changes it —
    # so a multithreaded bgfx cannot compile for wasm at all: it fails on
    # "no type named 'Thread' in namespace 'bx'". That is fine, because the
    # render thread is exactly what we do not want on the web anyway, and
    # app.cpp already forces single-threaded submission everywhere by calling
    # bgfx::renderFrame() before bgfx::init().
    if(EMSCRIPTEN)
        set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
    else()
        set(BGFX_CONFIG_MULTITHREADED ON CACHE BOOL "" FORCE)
    endif()
    # shaderc must be a host-native binary even when cross-compiling to
    # wasm/iOS: it reads .sc files off the real filesystem, so a shaderc built
    # for the target runs under node inside Emscripten's virtual filesystem and
    # fails with "Unable to open file". When DRIFTFALL_SHADERC points at a
    # host build, do not build bgfx's own tools at all — Shaders.cmake imports
    # that binary as bgfx::shaderc instead.
    if(DRIFTFALL_SHADERC)
        set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    else()
        set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(bgfx)

    # Shaders.cmake wires up the compile rules for our own .sc files, which a
    # host-tools configure has no interest in — it only wants the compiler.
    if(NOT DRIFTFALL_HOST_TOOLS_ONLY)
        include(Shaders)
    endif()
endif()
