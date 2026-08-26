<!-- Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0 -->

# Contributing to DRIFTFALL

## Getting set up

```bash
cmake -S . -B build -G Ninja -DDRIFTFALL_BUILD_CLIENT=OFF -DDRIFTFALL_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

That is the loop you will spend most of your time in. It needs no graphics
dependencies and takes about a minute from scratch.

## What a good change looks like

**Put logic in the headless half.** If it can live in `core/`, `voxel/`, or
`game/`, it should. Those directories never include a graphics header, which is
what keeps the game testable. Rendering code exists to draw what the simulation
already decided.

**Test the feel, not just the function.** The active-reload state machine has
tests for mashing, for jamming, for the bonus expiring. Gunfeel is too
important to be verifiable only by playing.

**Justify budgets in comments.** Numbers like "4 chunks per frame" or "220
enemies" are derived from a frame budget, not from taste. If you change one,
change the comment explaining where the new number came from.

**Match the surrounding style.** Run `clang-format` — CI checks it:

```bash
find src tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
```

## Performance rules

This game targets a phone, and the constraint that actually bites is thermal,
not peak throughput. Before adding work to a frame:

- Does it run every frame, or only when something changed?
- Can it be paced across frames instead of done all at once?
- Does it allocate? Per-frame allocation in the mesher or the sim is a bug.
- Does it saturate every core? We deliberately leave headroom.

## Licensing

- Code contributions are licensed under **Apache-2.0** (see `LICENSE`).
- Asset contributions are licensed under **CC BY 4.0** (see `assets/LICENSE`).
- Do not add assets you did not create unless their license permits
  redistribution under those terms. Record provenance in `assets/CREDITS.md`.
- New third-party dependencies must be permissively licensed (MIT, BSD, zlib,
  Apache-2.0, or public domain) and added to `NOTICE`. **No GPL or LGPL** —
  it is incompatible with shipping on the App Store.

Pin every dependency to a tag. A floating dependency is a build that breaks on
someone else's machine.

## Filing a bug

Runs are deterministic in their seed. Include the seed and the wave number and
the bug is usually reproducible in one line.
