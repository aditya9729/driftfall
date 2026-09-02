$input v_color0

// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// A passthrough, deliberately.
//
// The HUD is composited in *display space*, after the scene has finished. Both
// fs_chunk and fs_sky apply the shared exposure, tonemap with ACES and encode
// to sRGB themselves, so by the time view 2 runs the backbuffer already holds
// finished display-space pixels — there is no linear HDR target to join. The
// vertex colours are therefore authored as sRGB bytes and written straight out.
//
// Sending the HUD through the scene's tonemap instead would be actively wrong,
// twice over. A readout authored at 0.85 comes back at roughly 0.7 and the
// danger red turns salmon, which is the "washed out HUD" everyone recognises.
// Worse, it would couple legibility to exposure: retuning the sector's lighting
// would quietly change how readable the health bar is, and a health bar is the
// one thing in the frame whose contrast must not be an art-direction variable.
//
// No texture fetch, no branch, one interpolated varying. On a tiler the whole
// overlay is a handful of blended quads and costs nothing worth measuring.

#include <bgfx_shader.sh>

void main()
{
	gl_FragColor = v_color0;
}
