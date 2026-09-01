$input a_position, a_color0
$output v_normal, v_color0, v_wpos

// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// Unpacks the 8-byte chunk vertex written by src/voxel/greedy_mesher.cpp.
//   a_position.xyz = chunk-local integer position, 0..32
//   a_position.w   = (normal index << 2) | ambient occlusion level
//   a_color0.x     = material id
//   a_color0.y     = damage, 0..255
//
// All of it arrives normalised to 0..1 rather than as raw bytes; see
// render/chunk_mesh.cpp for why that is forced on us.

#include <bgfx_shader.sh>

uniform vec4 u_chunkOrigin;        // xyz = this chunk's world-space origin
uniform vec4 u_materialColor[8];   // indexed by material id

void main()
{
	vec3 localPos = a_position.xyz * 255.0;

	vec3 worldPos = localPos + u_chunkOrigin.xyz;
	gl_Position = mul(u_modelViewProj, vec4(worldPos, 1.0));
	v_wpos = worldPos;

	// Unpack the normal index and AO level out of the single packed byte.
	// Not named `packed`: that is a reserved word in GLSL and shaderc rejects
	// it outright.
	float normalAo = a_position.w * 255.0;
	float normalIndex = floor(normalAo * 0.25);
	float ao = normalAo - normalIndex * 4.0;

	float axis = floor(normalIndex * 0.5);
	float facing = ((normalIndex - axis * 2.0) < 0.5) ? 1.0 : -1.0;

	vec3 n = vec3(0.0, 0.0, 0.0);
	if (axis < 0.5)       { n = vec3(facing, 0.0, 0.0); }
	else if (axis < 1.5)  { n = vec3(0.0, facing, 0.0); }
	else                  { n = vec3(0.0, 0.0, facing); }
	v_normal = n;

	int materialIndex = int(a_color0.x * 255.0 + 0.5);
	vec3 base = u_materialColor[materialIndex].rgb;

	// The mesher currently writes a constant AO level, so this is flat until a
	// real occlusion term exists. Kept live so that enabling it is a one-line
	// change in the mesher rather than a shader change too.
	float aoTerm = 0.55 + 0.15 * ao;

	// Alpha carries damage through to the fragment stage, where it drives the
	// emissive crack glow. Damage is already 0..1 out of the normalised byte.
	v_color0 = vec4(base * aoTerm, a_color0.y);
}
