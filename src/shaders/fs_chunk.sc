$input v_normal, v_color0, v_wpos, v_ao

// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// Still deliberately cheap: no texture fetch anywhere, no loops, and every
// term below is a handful of ALU. On a mobile tiler fragment cost is the
// budget that runs out first, and a horde shooter needs to spend it on
// enemies and effects rather than on walls.
//
// The one thing worth paying for is *cell definition*. Greedy meshing merges a
// whole wall into a couple of enormous quads, so without a per-voxel term the
// scene is a handful of flat slabs — which is precisely what makes untextured
// voxels read as a 1990s software renderer. Reconstructing the voxel grid from
// the world position costs no extra geometry and no extra draw calls, and it
// is what makes a wall read as built out of blocks.

#include <bgfx_shader.sh>

uniform vec4 u_lightDir;       // xyz = direction the key light travels
uniform vec4 u_fogParams;      // x = start distance, y = density
uniform vec4 u_fogColor;
uniform vec4 u_eyePos;
uniform vec4 u_ambientSky;     // rgb = light from above,  w = exposure
uniform vec4 u_ambientGround;  // rgb = light from below
uniform vec4 u_keyColor;       // rgb = key light colour,  w = key intensity
uniform vec4 u_rimColor;       // rgb = rim colour,        w = rim intensity

// Hash Without Sine. Stable per voxel cell and cheap enough to run per pixel.
float hash13(vec3 p)
{
	p = fract(p * 0.1031);
	p += dot(p, p.yzx + 33.33);
	return fract((p.x + p.y) * p.z);
}

// Narkowicz's ACES approximation. Without a tonemap the key light clips to
// flat white the moment a surface faces it, which is a large part of why the
// untonemapped version read as a 90s renderer.
vec3 tonemap(vec3 x)
{
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
	vec3 n = normalize(v_normal);
	vec3 viewDir = normalize(u_eyePos.xyz - v_wpos);
	vec3 l = normalize(-u_lightDir.xyz);

	float dist = length(v_wpos - u_eyePos.xyz);

	// --- per-voxel cell definition -----------------------------------------
	// Faces are axis-aligned, so the two in-plane axes are simply the two world
	// axes the normal does not point along.
	vec3 an = abs(n);
	vec2 cellUv;
	if (an.x > 0.5)       { cellUv = v_wpos.zy; }
	else if (an.y > 0.5)  { cellUv = v_wpos.xz; }
	else                  { cellUv = v_wpos.xy; }

	vec2 f = fract(cellUv);
	vec2 toEdge = min(f, 1.0 - f);
	float edge = min(toEdge.x, toEdge.y);

	// Fade the grooves out with distance. They go sub-pixel far away, and a
	// sub-pixel dark line is just shimmer.
	float detail = clamp(1.0 - (dist - 26.0) / 70.0, 0.0, 1.0);
	float groove = mix(1.0, smoothstep(0.0, 0.05, edge), detail);

	// A stable value shift per voxel, so a merged quad stops being one flat
	// colour. Offsetting into the cell the face belongs to keeps the two sides
	// of a wall from sampling the same cell.
	vec3 cell = floor(v_wpos - n * 0.5);
	float grain = mix(1.0, 0.86 + 0.28 * hash13(cell), detail);

	vec3 albedo = v_color0.rgb * groove * grain;

	// --- lighting ----------------------------------------------------------
	// Hemispheric ambient rather than a flat fill: in vacuum almost nothing
	// bounces off the deck, but the sector is lit from above, and having the
	// two differ is what stops every surface reading as the same material.
	float upness = n.y * 0.5 + 0.5;
	vec3 ambient = mix(u_ambientGround.rgb, u_ambientSky.rgb, upness);

	// Occlusion attenuates ambient far more than direct light — a corner is
	// dark because the sky cannot reach it, not because the sun cannot. Using
	// one factor for both is what makes baked AO read as dirt smeared into the
	// creases rather than as shape.
	float aoAmbient = mix(0.10, 1.0, v_ao);
	float aoDirect = mix(0.48, 1.0, v_ao);

	float key = max(dot(n, l), 0.0);

	vec3 halfVec = normalize(l + viewDir);
	float spec = pow(max(dot(n, halfVec), 0.0), 48.0) * 0.4 * key * aoDirect;

	// Rim light picks the silhouette of cover out of the dark, which matters
	// more than it looks: cover is the whole tactical layer of this game.
	// A high exponent keeps it on the silhouette instead of spreading across
	// every large surface the camera happens to see edge-on.
	float fresnel = pow(1.0 - max(dot(n, viewDir), 0.0), 6.0);

	vec3 lit = albedo * (ambient * aoAmbient + u_keyColor.rgb * (key * u_keyColor.w * aoDirect));
	lit += u_keyColor.rgb * spec;
	lit += u_rimColor.rgb * (fresnel * u_rimColor.w * aoAmbient);

	// --- damage ------------------------------------------------------------
	// Damage reads as heat: plating glows along the cracks before it fails.
	float dmg = v_color0.a;
	lit = mix(lit, lit * 0.45, dmg * 0.7);
	lit += vec3(1.0, 0.38, 0.10) * (pow(dmg, 1.6) * 2.4);

	// --- atmosphere --------------------------------------------------------
	// Exponential rather than linear: linear fog has a visible onset plane that
	// sweeps across the deck as you walk, which reads as a bug.
	float fogAmt = 1.0 - exp(-max(dist - u_fogParams.x, 0.0) * u_fogParams.y);
	lit = mix(lit, u_fogColor.rgb, clamp(fogAmt, 0.0, 1.0));

	// --- output ------------------------------------------------------------
	lit *= u_ambientSky.w;  // exposure
	vec3 color = tonemap(lit);

	vec2 uv = (gl_FragCoord.xy - u_viewRect.xy) / max(u_viewRect.zw, vec2(1.0, 1.0));
	vec2 vd = uv - 0.5;
	color *= 1.0 - dot(vd, vd) * 0.5;

	// The palette and every light above are linear; the framebuffer is not.
	gl_FragColor = vec4(pow(color, vec3_splat(1.0 / 2.2)), 1.0);
}
