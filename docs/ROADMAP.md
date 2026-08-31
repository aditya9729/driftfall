<!-- Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0 -->

# DRIFTFALL — Roadmap

Every milestone has a **proof** — a thing that is either true or not. "Mostly
done" is not a state a milestone can be in.

---

## M0 — Foundation ✅

Repo, Apache-2.0, CI, the headless simulation core.

- [x] CMake build, pinned dependencies, warnings-as-errors option
- [x] `core/` — fixed timestep, logging, job system
- [x] `voxel/` — chunk storage with uniform fast path, sparse damage
- [x] `voxel/` — greedy mesher with cross-seam sampling and face ownership
- [x] `game/` — wave director, active reload, build economy, run state machine
- [x] 67 tests, CI on GCC/Clang/AppleClang + ASan/UBSan
- [x] Client shell: SDL3 window, bgfx init, camera, chunk upload, shaders

**Proof:** `ctest` is green and the web job produces a `.wasm`.

---

## M1 — It renders

- [ ] Verify the web build in a browser end to end
- [x] On-screen debug HUD: frame time, chunks drawn, remesh backlog, heap
- [ ] Touch camera and movement validated on a real phone
- [ ] Skybox / starfield so the sector reads as being in space
- [ ] Screen-space AO pass

**Proof:** You can fly through a generated sector on your phone at a locked
60 fps, and the HUD proves it.

> **Where this actually stands.** The client now compiles and links, and the
> shaders build for GLSL, ESSL and SPIR-V. None of that was true before: the
> shader step could not run at all, so no part of the client had ever been
> built by anything. The HUD's arithmetic is covered by unit tests, but no
> frame of this game has yet been drawn on a screen — that is precisely what
> the first and third boxes above are for, and neither is a code task.

---

## M2 — Gunfeel

The milestone that decides whether the game is worth finishing.

- [ ] Jolt Physics: character controller, gravity, collision against the voxel
      world via a simplified proxy
- [ ] Voxel raycast from the camera; the Salvage Rifle damages what it hits
- [ ] Destruction VFX: chunk debris, impact sparks, damage glow
- [ ] Active-reload UI — the window bar, on screen, readable at arm's length
- [ ] One enemy (Skitter): spawn, path, close, damage the player
- [ ] Haptics on hit, on perfect reload, on jam

**Proof:** It feels good with the sound off. If it does not, stop and fix
that before doing anything else on this list.

---

## M3 — The loop

- [ ] Build mode: target a voxel face, place Barricade / Reinforced
- [ ] Turret-bot deployment with a real model and firing behaviour
- [ ] Wave spawning at hull breach points, not the origin — the direction an
      assault arrives from should itself be information
- [ ] Full Prep → Assault → Cleared cycle playable
- [ ] Lancer and Breacher archetypes

**Proof:** A stranger plays three waves without being told how, and asks to
play a fourth.

---

## M4 — The run

- [ ] Run structure: 8–12 waves, then an extraction choice
- [ ] Between-run progression and unlocks
- [ ] Save/load (seed + progression, not a world snapshot)
- [ ] Breach Shotgun and Mining Lance
- [ ] Bulwark; the Warden boss and its wall-rebuilding behaviour
- [ ] Defeat and victory screens with a run summary

**Proof:** A ten-minute session feels like a complete thing, not a slice.

---

## M5 — Content and juice

- [ ] Audio: miniaudio, weapon layers, spatialised enemies, the derelict's
      ambient hum
- [ ] Three sector archetypes with distinct silhouettes and layouts
- [ ] Five enemy types fully modelled and animated
- [ ] Music that reacts to the phase
- [ ] Main menu, settings, accessibility pass (colourblind-safe materials,
      aim-assist strength, haptics toggle, left-handed layout)

**Proof:** Strangers play it without you explaining it.

---

## M6 — Ship

- [ ] Performance and thermal pass: 15-minute soak on the oldest target device
- [ ] Web build deployed to Cloudflare Pages with COOP/COEP headers
- [ ] iOS: Xcode target, app icons, launch screen, privacy manifest
- [ ] TestFlight beta
- [ ] App Store submission — 12+ rating, screenshots, preview video

**Proof:** Shipped.

---

## Things that are not on this roadmap, on purpose

- **Real-time multiplayer.** Netcode for a destructible voxel world is a
  project of its own. v1 ships single-player.
- **Infinite worlds.** Sectors stay bounded.
- **An in-game level editor.**
- **Anything that changes the age rating.**

Adding any of these is a decision to ship later. That is allowed — but it
should be made deliberately, not by accident.

## What you will need that code cannot provide

- An **Apple Developer Program** membership ($99/yr) for TestFlight and the
  App Store. Needed at M6, worth having by M2 to test on-device.
- **Art direction.** The codebase assumes flat-shaded voxels and a cold,
  desaturated palette so that muzzle flash, damage glow, and enemy optics are
  the only saturated things on screen. That is a real style, but it is a
  placeholder for a real artist's decision.
