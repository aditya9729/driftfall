$input v_normal, v_color0, v_wpos

// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// Deliberately cheap: one directional key light, one fill, distance fog. No
// per-pixel texture fetch at all — voxel faces are flat-shaded by material.
// On a mobile tiler, fragment cost is the budget that runs out first, and a
// horde shooter spends it on enemies and effects, not on walls.

#include <bgfx_shader.sh>

uniform vec4 u_lightDir;   // xyz = direction the key light travels
uniform vec4 u_fogParams;  // x = start, y = range, rgb of u_fogColor below
uniform vec4 u_fogColor;
uniform vec4 u_eyePos;

void main()
{
	vec3 n = normalize(v_normal);
	vec3 l = normalize(-u_lightDir.xyz);

	float key = max(dot(n, l), 0.0);
	// Hemispheric fill so downward faces are readable instead of black. In
	// vacuum there is no bounce light, but an unreadable floor is worse than
	// a physically wrong one.
	float fill = 0.5 + 0.5 * n.y;

	vec3 lit = v_color0.rgb * (0.28 * fill + 0.85 * key);

	float dist = length(v_wpos - u_eyePos.xyz);
	float fog = clamp((dist - u_fogParams.x) / max(u_fogParams.y, 0.001), 0.0, 1.0);

	gl_FragColor = vec4(mix(lit, u_fogColor.rgb, fog), 1.0);
}
