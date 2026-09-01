$input v_ray

// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// The debris belt, procedurally. No cubemap: a skybox texture large enough not
// to look soft on a phone is several megabytes of the memory budget, and the
// web build deliberately has no asset-loading path at all.
//
// Drawn last, with a depth test rather than first as a background, so on a
// tiler every pixel the sector already covered is rejected before this shader
// runs. Inside the hull that is almost the whole screen.

#include <bgfx_shader.sh>

uniform vec4 u_skyHorizon;  // rgb, w = exposure (matches the chunk shader)
uniform vec4 u_skyZenith;
uniform vec4 u_skyNadir;

float hash13(vec3 p)
{
	p = fract(p * 0.1031);
	p += dot(p, p.yzx + 33.33);
	return fract((p.x + p.y) * p.z);
}

// Value noise, one octave. Eight hashes is the most this is worth: the nebula
// only needs to break the gradient up, and it is behind everything.
float noise13(vec3 p)
{
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);

	float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
	float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
	float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
	float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
	float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
	float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
	float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
	float n111 = hash13(i + vec3(1.0, 1.0, 1.0));

	return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
	           mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y),
	           f.z);
}

vec3 tonemap(vec3 x)
{
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
	vec3 d = normalize(v_ray);

	// --- gradient ----------------------------------------------------------
	float up = clamp(d.y, 0.0, 1.0);
	float down = clamp(-d.y, 0.0, 1.0);
	vec3 col = mix(u_skyHorizon.rgb, u_skyZenith.rgb, pow(up, 0.6));
	col = mix(col, u_skyNadir.rgb, pow(down, 0.5));

	// --- nebula ------------------------------------------------------------
	// Kept dim, cold and wispy. It is there to stop the gradient reading as a
	// flat wash, not to be looked at. Two octaves because one at this scale is
	// a handful of soft blobs that read as weather rather than as deep space,
	// and a tight smoothstep so most of the sky stays empty.
	float neb = noise13(d * 4.5) * 0.65 + noise13(d * 11.0) * 0.35;
	neb = smoothstep(0.56, 0.92, neb);
	col += vec3(0.030, 0.048, 0.105) * neb * (0.3 + 0.7 * up);

	// --- stars -------------------------------------------------------------
	// One cell per star, with a hashed offset inside it so the field does not
	// read as a grid.
	vec3 sp = d * 170.0;
	vec3 id = floor(sp);
	vec3 gv = fract(sp) - 0.5;

	float h = hash13(id);
	if (h > 0.972) {
		vec3 off = vec3(hash13(id + 1.7), hash13(id + 3.1), hash13(id + 5.3)) - 0.5;
		float dd = length(gv - off * 0.7);
		float bright = (h - 0.972) / 0.028;
		float star = smoothstep(0.17, 0.0, dd) * bright;

		// Real starfields are not white. Bias hot/blue with a few warm ones.
		float temp = hash13(id + 9.4);
		vec3 starCol = mix(vec3(0.65, 0.78, 1.0), vec3(1.0, 0.82, 0.62), temp * temp);
		col += starCol * star * 2.2;
	}

	col *= u_skyHorizon.w;  // exposure, shared with the chunk shader
	vec3 color = tonemap(col);

	vec2 uv = (gl_FragCoord.xy - u_viewRect.xy) / max(u_viewRect.zw, vec2(1.0, 1.0));
	vec2 vd = uv - 0.5;
	color *= 1.0 - dot(vd, vd) * 0.5;

	gl_FragColor = vec4(pow(color, vec3_splat(1.0 / 2.2)), 1.0);
}
