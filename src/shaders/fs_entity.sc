$input v_normal, v_color0, v_wpos, v_lpos, v_optic

// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// The lighting chain here is fs_chunk's, term for term: hemispheric ambient, a
// single key, a Blinn specular, rim, exponential fog, exposure, ACES, gamma.
// That is not tidiness — an enemy lit by a second model reads as pasted onto
// the frame rather than standing in it, and it is the first thing anyone
// notices about box enemies. The uniforms are the same uniforms by name, so
// the art direction is still tuned in exactly one place (renderer.cpp).
//
// What is *not* inherited is the per-voxel groove and grain. Those reconstruct
// the voxel grid from the world position, which is true of a wall and false of
// a machine; running them here would put a voxel lattice on every enemy.
//
// Still no texture fetch and no loops. Two hundred and twenty of these overlap
// heavily in screen space, so fragment cost here is paid many times over.

#include <bgfx_shader.sh>

uniform vec4 u_lightDir;       // xyz = direction the key light travels
uniform vec4 u_fogParams;      // x = start distance, y = density
uniform vec4 u_fogColor;
uniform vec4 u_eyePos;
uniform vec4 u_ambientSky;     // rgb = light from above,  w = exposure
uniform vec4 u_ambientGround;  // rgb = light from below
uniform vec4 u_keyColor;       // rgb = key light colour,  w = key intensity
uniform vec4 u_rimColor;       // rgb = rim colour,        w = rim intensity

// Narkowicz's ACES approximation, same as fs_chunk. Two copies of ten
// characters is cheaper than a shared include that shaderc has to be told
// about on every platform.
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
	float flash = v_color0.a;

	// --- chassis -----------------------------------------------------------
	// One plate seam around the body at a fixed fraction of its height. It
	// scales with the entity instead of tiling, so it never turns into moire on
	// a large one, and it is the cheap analogue of the cell definition fs_chunk
	// pays for: without *some* internal break a box is one flat slab, which is
	// most of what makes untextured geometry look like a 1990s renderer.
	float seam = smoothstep(0.02, 0.06, abs(abs(v_lpos.y) - 0.70));
	vec3 albedo = v_color0.rgb * mix(0.82, 1.0, seam);

	// A hit whitens the plating as well as adding light below. At two hundred
	// bodies the flash is often the only cue telling the player which one they
	// actually hit, so it has to survive being one of many.
	albedo = mix(albedo, vec3_splat(1.0), flash * 0.55);

	// --- lighting ----------------------------------------------------------
	float upness = n.y * 0.5 + 0.5;
	vec3 ambient = mix(u_ambientGround.rgb, u_ambientSky.rgb, upness);

	// There is no mesher to bake occlusion for an entity, so stand in for it
	// with the one thing always true of a machine standing on a deck: the
	// nearer its feet, the less sky reaches it. Ambient only, for the same
	// reason fs_chunk keeps aoAmbient and aoDirect apart — a corner is dark
	// because the sky cannot get in, not because the key light cannot.
	float aoAmbient = mix(0.42, 1.0, clamp(v_lpos.y * 0.5 + 0.5, 0.0, 1.0));

	float key = max(dot(n, l), 0.0);

	vec3 halfVec = normalize(l + viewDir);
	float spec = pow(max(dot(n, halfVec), 0.0), 48.0) * 0.4 * key;

	float fresnel = pow(1.0 - max(dot(n, viewDir), 0.0), 6.0);

	// The one term deliberately weighted differently from fs_chunk. Rim is kept
	// weak on the world because a wall seen edge-on has a fresnel near 1 across
	// its whole area and the highlight becomes a blue wash. A box a metre across
	// has no such surface, and rim is exactly what separates a charging Bulwark
	// from the dark bulkhead behind it. A hit pushes it further still, so the
	// whole silhouette flares rather than only the side facing the key.
	float rim = fresnel * (u_rimColor.w * 2.4 + flash * 1.6);

	vec3 lit = albedo * (ambient * aoAmbient + u_keyColor.rgb * (key * u_keyColor.w));
	lit += u_keyColor.rgb * spec;
	lit += u_rimColor.rgb * rim;

	// --- optic -------------------------------------------------------------
	// DESIGN.md's rule is that the only saturated colour in frame is muzzle
	// flash, damage glow and enemy optics, so the chassis colours are cold and
	// flat and this is where an entity gets its identity.
	//
	// The forward face is the one where the local z coordinate is the largest
	// component. On a box of any proportions that test is exact rather than a
	// tolerance, because a face's own axis is pinned at +-1 while the other two
	// interpolate inside it. Consequence worth having: a machine facing away
	// shows no optic, so whether it has seen you is readable from behind.
	float front = step(max(abs(v_lpos.x), abs(v_lpos.y)), v_lpos.z);
	float visor = (1.0 - smoothstep(0.10, 0.20, abs(v_lpos.y - 0.40)))
	            * (1.0 - smoothstep(0.52, 0.64, abs(v_lpos.x)));
	lit += v_optic * (front * visor);

	// --- atmosphere --------------------------------------------------------
	float fogAmt = 1.0 - exp(-max(dist - u_fogParams.x, 0.0) * u_fogParams.y);
	lit = mix(lit, u_fogColor.rgb, clamp(fogAmt, 0.0, 1.0));

	// The hit flash is added *after* fog, unlike fs_chunk's damage glow, and
	// that break with physicality is the point. A Lancer at the far end of a
	// corridor is mostly fog; if its flash fogs with it the player cannot tell a
	// hit from a miss at exactly the range where they cannot see the health bar
	// either. Squared, so the tail collapses fast and the response reads as a
	// snap rather than a fade.
	lit += vec3(1.0, 0.74, 0.48) * (flash * flash * 2.6);

	// --- output ------------------------------------------------------------
	lit *= u_ambientSky.w;  // exposure
	vec3 color = tonemap(lit);

	// Same vignette as the world. Skipping it would make an enemy at the edge of
	// the screen brighter than the wall it is standing against.
	vec2 uv = (gl_FragCoord.xy - u_viewRect.xy) / max(u_viewRect.zw, vec2(1.0, 1.0));
	vec2 vd = uv - 0.5;
	color *= 1.0 - dot(vd, vd) * 0.5;

	gl_FragColor = vec4(pow(color, vec3_splat(1.0 / 2.2)), 1.0);
}
