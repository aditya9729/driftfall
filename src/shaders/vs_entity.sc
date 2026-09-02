$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_normal, v_color0, v_wpos, v_lpos, v_optic

// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// One static unit cube, positioned, scaled and rotated per instance. Nothing
// about an entity's size lives in here: the box arrives at half-extent 1 and
// the instance's half extents are what give a Bulwark its bulk and a Skitter
// its thinness. Silhouette is gameplay information in this game, so it has to
// stay entirely the caller's to choose.
//
//   a_position.xyz = cube corner, -1..1
//   a_position.w   = face normal index, encoded as greedy_mesher.cpp encodes it
//                    (axis * 2 + (positive ? 0 : 1))
//   i_data0        = xyz world position,               w = cos(yaw)
//   i_data1        = xyz half extents,                 w = sin(yaw)
//   i_data2        = rgb chassis colour, linear,       w = damage flash 0..1
//   i_data3        = rgb optic colour, linear,         w = fallback selector
//
// yaw's sine and cosine are computed on the CPU. A box is 24 vertices, so
// doing it here would cost the trig 24 times per entity to save 8 bytes of
// instance data.

#include <bgfx_shader.sh>

/// Mirror of one instance, used only when the backend cannot instance. See
/// EntityRenderer::draw().
uniform vec4 u_entityInstance[4];

void main()
{
	// The same four vec4s, read from the vertex stream when the backend reports
	// BGFX_CAPS_INSTANCING and from a uniform when it does not. Written as a
	// branch on a uniform rather than a mix(): the branch is uniform across the
	// whole draw so it costs nothing measurable, and unlike a mix it means an
	// unbound instance attribute cannot contribute a value at all.
	vec4 d0 = i_data0;
	vec4 d1 = i_data1;
	vec4 d2 = i_data2;
	vec4 d3 = i_data3;
	if (u_entityInstance[3].w > 0.5)
	{
		d0 = u_entityInstance[0];
		d1 = u_entityInstance[1];
		d2 = u_entityInstance[2];
		d3 = u_entityInstance[3];
	}

	vec3 halfExtents = d1.xyz;
	float cosYaw = d0.w;
	float sinYaw = d1.w;

	// Kept normalised to the box rather than scaled. The fragment shader places
	// the optic as a fraction of the body, so it lands in the same spot on a
	// Skitter and on a Warden without either one knowing the other's size.
	vec3 lp = a_position.xyz;
	v_lpos = lp;

	// Yaw about +Y, matching Camera: yaw 0 faces +Z, yaw pi/2 faces +X. Only
	// yaw, because these are machines walking a deck and a full orientation
	// would cost two more floats per instance to express something the
	// simulation never produces.
	vec3 scaled = lp * halfExtents;
	vec3 rotated = vec3( scaled.x * cosYaw + scaled.z * sinYaw,
	                     scaled.y,
	                    -scaled.x * sinYaw + scaled.z * cosYaw);

	vec3 worldPos = d0.xyz + rotated;
	gl_Position = mul(u_modelViewProj, vec4(worldPos, 1.0));
	v_wpos = worldPos;

	// Unpack the face normal — the same three lines as vs_chunk, except the
	// index arrives as a plain float because this attribute is float. (An
	// unnormalised integer attribute is the WebGL2 trap chunk_mesh.cpp
	// documents; there is no reason to go near it for 24 vertices.)
	float normalIndex = a_position.w;
	float axis = floor(normalIndex * 0.5);
	float facing = ((normalIndex - axis * 2.0) < 0.5) ? 1.0 : -1.0;

	vec3 n = vec3(0.0, 0.0, 0.0);
	if (axis < 0.5)       { n = vec3(facing, 0.0, 0.0); }
	else if (axis < 1.5)  { n = vec3(0.0, facing, 0.0); }
	else                  { n = vec3(0.0, 0.0, facing); }

	// The normal takes the same matrix as the position, with no inverse
	// transpose. A yaw rotation is orthonormal, and the non-uniform half extents
	// are an axis-aligned scale applied to an axis-aligned normal, which after
	// renormalisation leaves it exactly where it started.
	v_normal = vec3( n.x * cosYaw + n.z * sinYaw,
	                 n.y,
	                -n.x * sinYaw + n.z * cosYaw);

	// rgb chassis, a damage flash. Alpha carries the hit response through to the
	// fragment stage, the same way v_color0.a carries voxel damage in vs_chunk.
	v_color0 = d2;

	// Intensity is already folded into the colour on the CPU, so this is a plain
	// additive emissive by the time it gets to the fragment shader.
	v_optic = d3.rgb;
}
