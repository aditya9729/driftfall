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

#include <bgfx_shader.sh>

uniform vec4 u_chunkOrigin;        // xyz = this chunk's world-space origin
uniform vec4 u_materialColor[8];   // indexed by material id

void main()
{
	// The vertex attributes arrive normalised to 0..1 rather than as raw bytes
	// — see render/chunk_mesh.cpp for why that is forced on us — so scale back
	// to the byte values the greedy mesher actually wrote.
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

	// Damage reads as heat: plating glows along the cracks before it fails.
	// Already 0..1 straight out of the normalised attribute.
	float damage = a_color0.y;
	vec3 tint = mix(base, vec3(1.0, 0.42, 0.12), damage * 0.75);

	float aoTerm = 0.55 + 0.15 * ao;
	v_color0 = vec4(tint * aoTerm, 1.0);
}
