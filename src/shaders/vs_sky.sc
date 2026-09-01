$input a_position
$output v_ray

// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// A single fullscreen triangle. Rather than inverting the view-projection —
// which has to be got right separately for every clip-space convention bgfx
// targets — the camera basis is handed in directly and the view ray is built
// from it. Same result, no matrix inverse, and nothing to get backwards on one
// backend and not another.

#include <bgfx_shader.sh>

uniform vec4 u_camRight;    // xyz = right,   w = tan(fovY / 2) * aspect
uniform vec4 u_camUp;       // xyz = up,      w = tan(fovY / 2)
uniform vec4 u_camForward;  // xyz = forward

void main()
{
	// z = w = 1 puts the triangle on the far plane under either depth
	// convention, so the sky loses the depth test everywhere the sector
	// already drew.
	gl_Position = vec4(a_position.xy, 1.0, 1.0);

	v_ray = u_camForward.xyz + u_camRight.xyz * (a_position.x * u_camRight.w) +
	        u_camUp.xyz * (a_position.y * u_camUp.w);
}
