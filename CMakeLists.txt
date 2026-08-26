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
include(Dependencies)

# ---------------------------------------------------------------------------
# df_core — math, logging, time, job system. No dependencies beyond glm.
# ---------------------------------------------------------------------------
add_library(df_core STATIC
    src/core/log.cpp
    src/core/clock.cpp
    src/core/job_system.cpp
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
            "-sFILESYSTEM=0"
            "-sPTHREAD_POOL_SIZE=3"
            "-pthread"
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        )
        target_compile_options(driftfall PRIVATE -pthread)
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
