# Copyright 2026 Aditya Gudal
# SPDX-License-Identifier: Apache-2.0
#
# Compiles the .sc shaders into C headers with bgfx's shaderc, one variant per
# backend. Headers rather than runtime files means no asset loading path is
# needed for the shaders at all — which matters on the web build, where we
# deliberately run with -sFILESYSTEM=0.

include(bgfxToolUtils)

set(DRIFTFALL_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")
set(DRIFTFALL_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
file(MAKE_DIRECTORY "${DRIFTFALL_SHADER_OUT}")

# bgfx_compile_shader_to_header emits one header per shader containing every
# backend profile as a byte array, which is what BGFX_EMBEDDED_SHADER expects.
bgfx_compile_shader_to_header(
    TYPE VERTEX
    SHADERS "${DRIFTFALL_SHADER_DIR}/vs_chunk.sc"
    VARYING_DEF "${DRIFTFALL_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${DRIFTFALL_SHADER_OUT}"
)

bgfx_compile_shader_to_header(
    TYPE FRAGMENT
    SHADERS "${DRIFTFALL_SHADER_DIR}/fs_chunk.sc"
    VARYING_DEF "${DRIFTFALL_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${DRIFTFALL_SHADER_OUT}"
)

add_custom_target(df_shaders DEPENDS
    "${DRIFTFALL_SHADER_OUT}/vs_chunk.sc.bin.h"
    "${DRIFTFALL_SHADER_OUT}/fs_chunk.sc.bin.h"
)
