# Copyright 2026 Aditya Gudal
# SPDX-License-Identifier: Apache-2.0
#
# Compiles the .sc shaders into C headers with bgfx's shaderc, one variant per
# backend. Headers rather than runtime files means no asset loading path is
# needed for the shaders at all — which matters on the web build, where we
# deliberately run with -sFILESYSTEM=0.

# bgfx.cmake ships bgfxToolUtils in its own cmake/ directory but does not put
# that directory on CMAKE_MODULE_PATH for consumers, so include() cannot find
# it until we add it ourselves.
if(NOT bgfx_SOURCE_DIR)
    message(FATAL_ERROR "Shaders.cmake must be included after FetchContent_MakeAvailable(bgfx)")
endif()
list(APPEND CMAKE_MODULE_PATH "${bgfx_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# shaderc has to run on the machine doing the build.
#
# bgfxToolUtils invokes the `bgfx::shaderc` target. When cross-compiling, bgfx
# builds that target for the *target* — so `emcmake` produces a shaderc.js that
# node runs inside Emscripten's virtual filesystem, where the real paths to our
# .sc files do not exist. It fails with "Unable to open file", having compiled
# everything else first.
#
# So when cross-compiling, a host-native shaderc must be supplied:
#
#   cmake -S . -B build-host -DDRIFTFALL_BUILD_CLIENT=ON -DDRIFTFALL_BUILD_TESTS=OFF
#   cmake --build build-host --target shaderc
#   emcmake cmake -S . -B build-web ... \
#       -DDRIFTFALL_SHADERC=$PWD/build-host/_deps/bgfx-build/cmake/bgfx/shaderc
# ---------------------------------------------------------------------------
if(DRIFTFALL_SHADERC)
    if(NOT EXISTS "${DRIFTFALL_SHADERC}")
        message(FATAL_ERROR "DRIFTFALL_SHADERC is set to '${DRIFTFALL_SHADERC}', which does not exist")
    endif()
    if(NOT TARGET bgfx::shaderc)
        add_executable(bgfx::shaderc IMPORTED GLOBAL)
        set_target_properties(bgfx::shaderc PROPERTIES IMPORTED_LOCATION "${DRIFTFALL_SHADERC}")
    endif()
    message(STATUS "  shaderc ......... ${DRIFTFALL_SHADERC} (host-supplied)")
elseif(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
        "Cross-compiling without DRIFTFALL_SHADERC.\n"
        "bgfx would build shaderc for the target, and a target-built shaderc cannot read "
        "the .sc files off this filesystem. Build a host-native shaderc first and pass it:\n"
        "  cmake -S . -B build-host -DDRIFTFALL_BUILD_CLIENT=ON -DDRIFTFALL_BUILD_TESTS=OFF\n"
        "  cmake --build build-host --target shaderc\n"
        "  <cross-cmake> ... -DDRIFTFALL_SHADERC=${CMAKE_SOURCE_DIR}/build-host/_deps/bgfx-build/cmake/bgfx/shaderc")
endif()

include(bgfxToolUtils)


# bgfx_shader.sh — which defines mul(), u_modelViewProj and the rest of the
# cross-backend shader prelude — has to be on shaderc's include path. bgfx.cmake
# only sets BGFX_SHADER_INCLUDE_PATH in its installed package config, so a
# FetchContent consumer gets an empty include list and every shader fails on
# "`u_modelViewProj' undeclared". Point at it directly.
set(DRIFTFALL_BGFX_SHADER_INCLUDE "${bgfx_SOURCE_DIR}/bgfx/src")
if(NOT EXISTS "${DRIFTFALL_BGFX_SHADER_INCLUDE}/bgfx_shader.sh")
    message(FATAL_ERROR "bgfx_shader.sh not found under ${DRIFTFALL_BGFX_SHADER_INCLUDE}")
endif()

set(DRIFTFALL_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")
set(DRIFTFALL_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
file(MAKE_DIRECTORY "${DRIFTFALL_SHADER_OUT}")

# ---------------------------------------------------------------------------
# Two profile lists, because there are genuinely two.
#
# bgfx_compile_shader_to_header picks which profiles to *build* from the host
# platform, and emits one header per profile — vs_chunk.sc.glsl.bin.h and
# friends. BGFX_EMBEDDED_SHADER, meanwhile, expands to a table naming every
# profile its *target* platform supports (see bgfx/embedded_shader.h), and it
# wants them all visible from a single include.
#
# The two sets are not equal. On Linux the macro names a DXBC blob that shaderc
# on Linux cannot produce — Direct3D11 is simply never selected there, but the
# symbol still has to exist for the macro to compile. So we spell out both sets
# and aggregate them ourselves.
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    set(DRIFTFALL_SHADER_BUILT glsl essl spv)
    set(DRIFTFALL_SHADER_NEEDED essl spv)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(DRIFTFALL_SHADER_BUILT glsl essl spv mtl)
    set(DRIFTFALL_SHADER_NEEDED essl mtl)
elseif(APPLE)
    set(DRIFTFALL_SHADER_BUILT glsl essl spv mtl)
    set(DRIFTFALL_SHADER_NEEDED glsl essl spv mtl)
elseif(WIN32)
    set(DRIFTFALL_SHADER_BUILT glsl essl spv dx10 dx11)
    set(DRIFTFALL_SHADER_NEEDED glsl essl spv dx11)
else()
    set(DRIFTFALL_SHADER_BUILT glsl essl spv)
    set(DRIFTFALL_SHADER_NEEDED glsl essl spv dx11)
endif()

# Writes the single header renderer.cpp includes, pulling in every profile the
# embedded-shader macro will reference on this platform.
function(driftfall_aggregate_shader_header SHADER_NAME SHADER_TYPE)
    if(SHADER_TYPE STREQUAL "VERTEX")
        # bgfx's shader chunk magic, matching the inert blob it uses itself for
        # RendererType::Noop.
        set(stub_bytes "0x56, 0x53, 0x48, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00")
    else()
        set(stub_bytes "0x46, 0x53, 0x48, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00")
    endif()

    set(body "// Generated by cmake/Shaders.cmake. Do not edit.\n")
    string(APPEND body "#pragma once\n\n#include <cstdint>\n\n")

    foreach(profile IN LISTS DRIFTFALL_SHADER_NEEDED)
        if(profile IN_LIST DRIFTFALL_SHADER_BUILT)
            string(APPEND body "#include \"${SHADER_NAME}.sc.${profile}.bin.h\"\n")
        else()
            string(APPEND body "\n")
            string(APPEND body "// shaderc cannot produce a '${profile}' binary on this host and the\n")
            string(APPEND body "// matching bgfx backend is never selected here, but\n")
            string(APPEND body "// BGFX_EMBEDDED_SHADER still names the symbol. An inert placeholder\n")
            string(APPEND body "// keeps the macro well-formed without pretending the backend exists.\n")
            string(APPEND body "static const uint8_t ${SHADER_NAME}_${profile}[10] = {${stub_bytes}};\n")
        endif()
    endforeach()

    file(WRITE "${DRIFTFALL_SHADER_OUT}/${SHADER_NAME}.sc.bin.h" "${body}")
endfunction()

bgfx_compile_shader_to_header(
    TYPE VERTEX
    SHADERS "${DRIFTFALL_SHADER_DIR}/vs_chunk.sc"
    VARYING_DEF "${DRIFTFALL_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${DRIFTFALL_SHADER_OUT}"
    INCLUDE_DIRS "${DRIFTFALL_BGFX_SHADER_INCLUDE}"
    OUT_FILES_VAR DRIFTFALL_VS_OUTPUTS
)

bgfx_compile_shader_to_header(
    TYPE FRAGMENT
    SHADERS "${DRIFTFALL_SHADER_DIR}/fs_chunk.sc"
    VARYING_DEF "${DRIFTFALL_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${DRIFTFALL_SHADER_OUT}"
    INCLUDE_DIRS "${DRIFTFALL_BGFX_SHADER_INCLUDE}"
    OUT_FILES_VAR DRIFTFALL_FS_OUTPUTS
)

driftfall_aggregate_shader_header(vs_chunk VERTEX)
driftfall_aggregate_shader_header(fs_chunk FRAGMENT)

add_custom_target(df_shaders DEPENDS ${DRIFTFALL_VS_OUTPUTS} ${DRIFTFALL_FS_OUTPUTS})
