$input a_position, a_color0
$output v_color0

// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// The HUD's entire vertex stage. Positions arrive in *virtual pixels* — a
// canvas 540 units tall, anchored to the real corners — and u_modelViewProj is
// the orthographic matrix PlayerHud builds for that canvas, picked against
// bgfx's homogeneousDepth cap so the same geometry survives clipping on GL and
// on Metal/Vulkan alike.
//
// a_position is declared vec4 in varying.def.sc (it is shared with the chunk
// and sky shaders) while the HUD layout supplies only two floats. GL fills the
// remainder with (0, 0, 1), so .xy is the only part that means anything here.
//
// a_color0 arrives as a *normalised* Uint8x4, not an integer attribute. That is
// not a style choice — see the comment in render/chunk_mesh.cpp: on WebGL2 an
// unnormalised integer attribute against a vec4 declaration rejects every draw
// with GL_INVALID_OPERATION, and it does so silently.

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position.xy, 0.0, 1.0));
	v_color0 = a_color0;
}
