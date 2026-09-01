# Copyright 2026 Aditya Gudal
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.24)
project(driftfall VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# The simulation half of the game (core/, voxel/, game/) has no graphics
# dependencies and builds anywhere. The client half (render/, platform/) needs
# SDL3 + bgfx. Keeping these separable is what lets CI run the whole test suite
# on a headless runner in seconds.
option(DRIFTFALL_BUILD_CLIENT "Build the graphical client (SDL3 + bgfx)" ON)
option(DRIFTFALL_BUILD_TESTS  "Build the unit test suite" ON)
option(DRIFTFALL_WERROR       "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)

if(EMSCRIPTEN)
    # -pthread has to be set here, before the dependencies are declared, so it
    # reaches *every* target including the fetched ones. Emscripten objects
    # built with and without it are not link-compatible, and the link line
    # already asks for a pthread pool — previously only the driftfall target
    # itself got the flag, so SDL3, bgfx and even df_core disagreed with the
    # link. It is also what lets JobSystem use real workers on the web instead
    # of silently degrading to synchronous execution.
    add_compile_options(-pthread)
    add_link_options(-pthread)
endif()

include(Dependencies)

# ---------------------------------------------------------------------------
# df_core — math, logging, time, job system. No dependencies beyond glm.
# ---------------------------------------------------------------------------
add_library(df_core STATIC
    src/core/log.cpp
    src/core/clock.cpp
    src/core/job_system.cpp
    src/core/frame_stats.cpp
    src/core/mem_stats.cpp
)
target_include_directories(df_core PUBLIC src)
target_link_libraries(df_core PUBLIC glm::glm)
driftfall_set_warnings(df_core)

if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(df_core PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# df_voxel — chunk storage, palette compression, greedy meshing.
# ---------------------------------------------------------------------------
add_library(df_voxel STATIC
    src/voxel/chunk.cpp
    src/voxel/voxel_world.cpp
    src/voxel/greedy_mesher.cpp
)
target_link_libraries(df_voxel PUBLIC df_core)
driftfall_set_warnings(df_voxel)

# ---------------------------------------------------------------------------
# df_game — ECS simulation: run state, waves, weapons, build mode.
# ---------------------------------------------------------------------------
add_library(df_game STATIC
    src/game/sim.cpp
    src/game/wave_director.cpp
    src/game/weapon.cpp
)
target_link_libraries(df_game PUBLIC df_voxel EnTT::EnTT)
driftfall_set_warnings(df_game)

# ---------------------------------------------------------------------------
# driftfall — the client executable.
# ---------------------------------------------------------------------------
if(DRIFTFALL_BUILD_CLIENT)
    add_executable(driftfall
        src/main.cpp
        src/platform/app.cpp
        src/platform/input.cpp
        src/render/renderer.cpp
        src/render/camera.cpp
        src/render/chunk_mesh.cpp
        src/render/debug_hud.cpp
    )
    target_link_libraries(driftfall PRIVATE df_game SDL3::SDL3-static bgfx bx bimg)
    # The compiled shader headers land under ${CMAKE_CURRENT_BINARY_DIR}/generated.
    target_include_directories(driftfall PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    driftfall_set_warnings(driftfall)
    add_dependencies(driftfall df_shaders)

    if(EMSCRIPTEN)
        # -sASYNCIFY is deliberately absent: it costs 30-50% throughput. The main
        # loop is driven by emscripten_set_main_loop instead (see platform/app.cpp).
        #
        # Threads need SharedArrayBuffer, which needs COOP/COEP response headers.
        # GitHub Pages cannot set those; deploy the web build to a host that can
        # (Cloudflare Pages / Netlify) or serve web/coi-serviceworker.js.
        set(DRIFTFALL_EM_FLAGS
            "-sUSE_WEBGL2=1"
            "-sMIN_WEBGL_VERSION=2"
            "-sMAX_WEBGL_VERSION=2"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sINITIAL_MEMORY=268435456"
            "-sSTACK_SIZE=4194304"
            "-sEXIT_RUNTIME=0"
            # -sFILESYSTEM=0 is tempting here — the shaders are embedded as C
            # headers, so *our* code never touches a file on the web. But
            # SDL3's JS glue still emits FS.mkdir/open/write calls, and with
            # the filesystem stripped the page boots and then dies on
            # "FS is not defined" with a black canvas and no other symptom.
            "-sPTHREAD_POOL_SIZE=3"
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        )
        # -pthread is applied globally above, before the dependencies, because
        # it has to reach bx and SDL3 too.
        target_link_options(driftfall PRIVATE ${DRIFTFALL_EM_FLAGS})
        set_target_properties(driftfall PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    endif()

    if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set_target_properties(driftfall PROPERTIES
            MACOSX_BUNDLE ON
            MACOSX_BUNDLE_GUI_IDENTIFIER "dev.aditya9729.driftfall"
            MACOSX_BUNDLE_BUNDLE_NAME "DRIFTFALL"
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(DRIFTFALL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "")
message(STATUS "DRIFTFALL ${PROJECT_VERSION}")
message(STATUS "  build type ...... ${CMAKE_BUILD_TYPE}")
message(STATUS "  client (gfx) .... ${DRIFTFALL_BUILD_CLIENT}")
message(STATUS "  tests ........... ${DRIFTFALL_BUILD_TESTS}")
message(STATUS "  platform ........ ${CMAKE_SYSTEM_NAME}")
message(STATUS "")
