<!-- Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0 -->

# DRIFTFALL

**A voxel space-hulk horde shooter. Salvage. Fortify. Survive.**

You are a salvage-mech pilot boarding derelict stations in the debris belt.
Every wall is made of voxels you can mine, shoot through, or build out of — and
so is every piece of cover you are hiding behind. Strip the hull for materials,
print barricades and autonomous turret-bots, then hold the line when the
derelict's defence swarm wakes up.

Written in C++20, no engine. Runs in a browser via WebAssembly and on iOS via
Metal, from the same source.

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

> **Status: early.** The simulation layer — voxel storage, greedy meshing,
> the wave director, active reload, the build economy — is implemented and
> covered by 91 tests. The client renders: you can fly through a generated
> sector in a browser or on a desktop, with a debug HUD reporting frame time
> against the budget below. It has not yet been run on a phone.
> See [the roadmap](docs/ROADMAP.md).

---

## The design thesis

A voxel world is a destructible world. A destructible world makes cover
*dynamic*. Dynamic cover turns a horde shooter into something tactical instead
of twitchy — which is exactly what makes it work on a touchscreen, where
twitch aim is a losing bet.

Three things follow from that, and they are the whole design:

1. **Every wall is ammunition and every wall is temporary.** The shotgun that
   kills the thing charging you also opens the wall behind it.
2. **The loop is build-then-defend, not shoot-then-shoot.** Prep phase is where
   the decisions are; the wave is where you find out if they were right.
3. **Enemies are machines, never people.** A deliberate call: it keeps the age
   rating low, widens the audience, costs no gore technology, and is more
   on-theme than the alternative.

Read the full design in [docs/DESIGN.md](docs/DESIGN.md).

## Building

Requires **CMake 3.24+**, **Ninja**, and a **C++20** compiler (GCC 13+,
Clang 17+, or Xcode 15+). All dependencies are fetched by CMake — there is
nothing to install by hand.

### The simulation and its tests

No graphics dependencies, so this configures and runs in about a minute:

```bash
cmake -S . -B build -G Ninja -DDRIFTFALL_BUILD_CLIENT=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

### The client (desktop)

```bash
cmake -S . -B build -G Ninja -DDRIFTFALL_BUILD_CLIENT=ON
cmake --build build
./build/driftfall
```

The first configure clones and builds SDL3 and bgfx, which takes a while.

### The client (web)

Two steps, because `shaderc` reads `.sc` files off the real filesystem and so
has to be built for *this* machine. Built under `emcmake` it becomes a
`shaderc.js` running in Emscripten's virtual filesystem, where those paths do
not exist:

```bash
# 1. a host-native shaderc (only the one target, so this does not build SDL3)
cmake -S . -B build-host -G Ninja -DDRIFTFALL_BUILD_CLIENT=ON -DDRIFTFALL_BUILD_TESTS=OFF
cmake --build build-host --target shaderc

# 2. the wasm build, pointed at it
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDRIFTFALL_BUILD_CLIENT=ON -DDRIFTFALL_BUILD_TESTS=OFF \
  -DDRIFTFALL_SHADERC="$PWD/build-host/_deps/bgfx-build/cmake/bgfx/shaderc"
cmake --build build-web
```

Then play it:

```bash
python3 web/serve.py            # serves build-web on http://localhost:8080
```

> **Hosting note.** The web build uses WebAssembly threads, which require
> `SharedArrayBuffer`, which requires `Cross-Origin-Opener-Policy` and
> `Cross-Origin-Embedder-Policy` response headers. `web/serve.py` sets them,
> which is the entire reason it exists rather than `python -m http.server`.
> **GitHub Pages cannot set those headers.** Deploy to Cloudflare Pages,
> Netlify, or anything else that lets you configure headers.

## Controls

| | Touch | Keyboard / mouse |
|---|---|---|
| Move | Left half — floating stick, spawns under your thumb | `WASD` |
| Look | Right half — drag | Mouse |
| Fire | Right half — hold | Left mouse |
| Reload / active reload | Right half — tap | `R` |
| Debug HUD | Left half — tap with a second finger | `F3` |
| Build mode | (M3) | `B` |

Reload is a tap on the same half of the screen you aim with, on purpose: the
active-reload window is measured in frames, and no input that matters that
much should require moving your aiming thumb.

## Layout

```
src/core/      math, logging, fixed timestep, job system   — no dependencies
src/voxel/     chunk storage, greedy meshing               — headless, tested
src/game/      run state, waves, weapons, build economy    — headless, tested
src/render/    bgfx renderer, camera, GPU chunk meshes
src/platform/  SDL3 window, touch input, frame loop
src/shaders/   bgfx .sc shaders, compiled to headers at build time
tests/         doctest suite for everything headless
docs/          design, architecture, roadmap
```

The split down the middle is deliberate: `core` + `voxel` + `game` know nothing
about graphics, which is why the entire game loop is unit-testable and why CI's
gating job runs in seconds on a headless runner.

## Performance budget

These are targets, not aspirations, and CI is where they get enforced:

| Budget | Target | Why |
|---|---|---|
| Frame time | 16.6 ms on an iPhone 12 | 60 fps is the floor for a shooter |
| Chunk remeshing | ≤ 2.5 ms/frame, ≤ 4 chunks | one shotgun blast dirties four chunks |
| Draw calls | ≤ 300 | mobile tilers stall on state changes |
| Resident memory | < 700 MB | iOS jetsams above this on older devices |
| Thermal | steady state at minute 15 | phone games die at minute 12, not at 30 fps |

Phones do not fail at low frame rates. They fail at *falling* frame rates,
twelve minutes in, when the SoC throttles. Every architectural choice here —
bounded sectors, paced remeshing, a worker count that deliberately leaves cores
idle — comes from that.

## License

Source code is licensed under the [Apache License 2.0](LICENSE).

Non-code assets under `assets/` are licensed separately under
[CC BY 4.0](assets/LICENSE).

Third-party dependencies and their licenses are listed in [NOTICE](NOTICE).
All are permissive and compatible with redistribution under Apache-2.0.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
