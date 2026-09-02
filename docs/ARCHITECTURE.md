<!-- Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0 -->

# DRIFTFALL — Technical Architecture

## The one structural rule

```
   ┌───────────────────────────────┐    ┌──────────────────────────┐
   │  SIMULATION (headless)        │    │  CLIENT (graphics)       │
   │                               │    │                          │
   │  core/   math, clock, jobs    │◀───│  render/   bgfx, camera  │
   │  voxel/  chunks, meshing      │    │  platform/ SDL3, input   │
   │  game/   waves, weapons, econ │    │  shaders/  .sc → headers │
   │                               │    │                          │
   │  builds and tests anywhere    │    │  needs SDL3 + bgfx       │
   └───────────────────────────────┘    └──────────────────────────┘
              depends on nothing              depends on simulation
```

The simulation half never includes a graphics header. That is not tidiness —
it is what makes the entire game loop unit-testable, lets CI's gating job run
in seconds on a headless runner, and keeps a future headless replay/balance
tool possible for free.

## Dependencies

| Library | License | Why this one |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | zlib | One window/input/audio API that already has working iOS and Emscripten backends |
| [bgfx](https://github.com/bkaradzic/bgfx) | BSD-2 | **One** shader pipeline reaching Metal on iOS and WebGL2/WebGPU in the browser. This is the entire reason the renderer is not hand-written Metal. |
| [EnTT](https://github.com/skypjack/entt) | MIT | Fast, header-only ECS; the de-facto standard |
| [GLM](https://github.com/g-truc/glm) | MIT | Header-only math, GLSL-shaped |
| [doctest](https://github.com/doctest/doctest) | MIT | Fastest-compiling C++ test framework |

Everything is fetched by CMake and pinned to a tag. A floating dependency is a
build that breaks on someone else's machine.

## Voxel storage

### Chunks are 32³ with a uniform fast path

`Chunk` has two states:

- **Uniform** — every voxel is the same material. Stores one `Voxel`, allocates
  nothing. In a derelict station, the overwhelming majority of chunks are
  uniform vacuum.
- **Dense** — a `std::array<Voxel, 32768>`, materialised lazily the first time
  something writes a *differing* value.

Damage is stored sparsely (`unordered_map<u16, u8>`) for the same reason: at
any moment a handful of voxels are mid-destruction, not thirty thousand.

Together these keep a 224 × 64 × 224 sector well inside a phone's budget. The
test suite asserts it stays under 64 MB.

### Why sectors are bounded

The single most important scoping decision in the project. An infinite world
means chunk streaming, LOD, floating-origin rebasing for float precision, and a
save format that grows without limit — four systems, each of which can eat a
month.

A station sector is ~224 × 64 × 224 voxels. It fits in memory whole, serialises
in one shot, and is exactly as much space as a ten-minute horde run needs.

## Greedy meshing

`greedy_mesh()` implements Lysenko's algorithm with material and damage folded
into the merge key. For each of three axes it sweeps N+1 planes, builds a mask
of visible faces, and merges that mask into the fewest possible rectangles.

Two details that are easy to get wrong and expensive to debug:

**Sampling crosses chunk seams.** The mesher queries the `VoxelWorld`, not the
`Chunk`, so a face hidden behind a neighbouring chunk's voxel is never emitted.

**A face belongs to the voxel it grows out of.** On the two planes straddling a
chunk boundary, one side's owner lives in the *neighbouring* chunk — and that
neighbour emits the face itself. Without the ownership check, every seam in the
world is drawn twice, which ships as z-fighting shimmer along every chunk edge.
There is a dedicated regression test for exactly this.

Measured compression on a generated sector is well over 2× faces-to-quads, and
a test fails the build if it ever regresses below that.

### The 8-byte vertex

```
struct PackedVertex {          // 8 bytes
    u8 x, y, z;                // chunk-local, 0..32
    u8 normal_ao;              // normal index (3 bits) | AO level (2 bits)
    u8 material;
    u8 damage;                 // 0..255, drives the crack/glow overlay
    u8 pad0, pad1;
};
```

The naive layout — `float3` position + `float3` normal + `float2` uv — is 32
bytes. At tens of thousands of vertices per visible chunk across dozens of
chunks, that difference *is* the vertex-fetch bandwidth budget of a mobile GPU.
The vertex shader unpacks and offsets by the chunk origin.

### Ambient occlusion is baked into the mesh

Occlusion is per-vertex, computed at mesh time from the eight voxels ringing
each face in the plane it looks into, using the standard rule:

```
if (side1 && side2) return 0;
return 3 - (side1 + side2 + corner);
```

The early return is load-bearing: once two sides are solid the corner is
already sealed, and letting the diagonal voxel darken it further gives a
strictly darker pixel where three blocks meet than where two do, which reads
as a smudge rather than as a corner.

Two consequences, both handled:

**Occlusion joins the merge key.** Quads only merge when all four corner values
agree, or the shading tears across the seam.

**The quad's triangle diagonal is chosen, not fixed.** Splitting a quad
interpolates its corner values anisotropically — a value on the shared diagonal
reaches across the whole quad, one off it does not — so with unequal corners
the default diagonal shows a hard crease. The mesher flips it when
`ao[0] + ao[2] > ao[1] + ao[3]`, which on a generated sector fires on about 15%
of quads.

This reverses an earlier decision to do AO in a screen-space pass, and the
reason is worth recording, because it is a case where the general-purpose
answer is the wrong one:

| | measured |
|---|---|
| quads, sector-wide | 12,386 → 32,721 (2.64×) |
| face→quad compression | 21.7× → 8.2× (the test floor is 2×) |
| whole-sector mesh time | 224 ms → 267 ms (+19%) |
| draw calls | unchanged |
| extra GPU passes | none |

The original estimate — "roughly triples the quad count" — was accurate. Its
conclusion was not: compression lands at 8×, four times above the floor the
test guards, and the runtime cost is zero extra passes. Meanwhile screen-space
AO on a tile-based mobile GPU needs a depth prepass, two blur passes and a
blit, all of which are GMEM traffic on exactly the hardware this ships on.
Modern engines reach for GTAO because they cannot assume anything about their
geometry. A voxel game can: the occluders are a known 1-unit grid, so the
answer is computable exactly, once, at mesh time.

## Shading

Everything is forward-rendered and there is no offscreen target, which keeps
the whole frame inside one pass on a tile-based GPU.

**Cell definition is the load-bearing trick.** Greedy meshing merges a whole
wall into a couple of enormous quads, so a purely per-face shading model draws
a handful of flat slabs — which is exactly what makes untextured voxels read as
a 1990s software renderer. The fragment shader recovers the voxel grid from the
world position instead: `fract()` of the two in-plane world axes gives the
position inside the cell, which drives a groove at the cell edges, and a hash of
the cell index gives each voxel a stable value shift. It costs no extra
geometry, no extra draw call, and no texture fetch, and it is what makes a wall
read as built out of blocks. Both terms fade out with distance, because a
sub-pixel dark line is just shimmer.

**Lighting is linear, output is not.** The material palette is authored in
display space, converted to linear once at startup, lit, tonemapped (ACES), and
encoded back on write. Skipping any of that is what makes flat-shaded surfaces
look like plastic.

Ambient is hemispheric rather than a flat fill: in vacuum almost nothing bounces
off the deck, so having up and down differ is what stops every surface reading
as the same material. The rim term is deliberately weak — rim is *additive*, and
a large surface seen at a grazing angle has a fresnel of nearly 1 across its
whole area, so a strong rim stops being an edge highlight and becomes a wash
that buries both the material colour and the grooves underneath it.

**The sky is procedural and drawn last.** No cubemap: a skybox texture large
enough not to look soft on a phone is several megabytes of the memory budget,
and the web build deliberately has no asset-loading path. It is submitted after
the sector, in a later view, with a depth test against what the sector already
wrote — so on a tiler almost every pixel is rejected before the shader runs.
Inside the hull, the open ceiling is a sliver of the screen.

## Frame pacing

### Fixed timestep

The simulation runs at a fixed 60 Hz regardless of display rate. Phones hand us
wildly variable frame times — 120 Hz ProMotion one second, 22 Hz mid-throttle
the next — and a variable-`dt` simulation makes gunplay, recoil, and the
active-reload window feel different on every device. Unacceptable for a
shooter.

`FixedTimestep` caps at 5 steps per frame and then **discards the backlog**
rather than carrying it. Carrying it means the next frame inherits the debt and
the game runs in slow motion; this is the classic spiral of death.

### Remeshing is paced, not immediate

The renderer's real job is not drawing — it is pacing remeshing. One shotgun
blast can dirty four chunks at once, and rebuilding all of them in the frame
they were dirtied is a guaranteed hitch.

So: dirty chunks queue, at most `kRemeshBudgetPerFrame` (4) are rebuilt per
frame, and duplicate queue entries collapse. The world is briefly one frame
stale instead of briefly at 20 fps.

### Measuring the pacing

`FrameStats` (in `core/`, so it is testable) keeps a four-second window of
frame times and reports the mean, a nearest-rank percentile, and a high-water
mark. The percentile is the point of it: a window alternating 8 ms and 25 ms
averages to a comfortable 16.5 ms while feeling terrible, so an average alone
would have said everything was fine.

Two things are deliberately kept apart, because they look identical from the
outside and mean opposite things:

- A **dropped step** is the device failing to keep up, and `FixedTimestep`
  discarding backlog rather than spiralling.
- A **stall** is the app not running at all — a backgrounded tab, a suspended
  app, a breakpoint. Folding one into the window would poison every figure for
  the next four seconds, so stalls are counted and excluded.

`worst_ms` is not windowed, on purpose. Thermal throttling is a slow drift over
minutes, and a four-second window forgets it by design.

### The job system leaves cores idle on purpose

`JobSystem` clamps to `min(hardware_concurrency - 2, 3)` workers. Saturating
every core on a phone is how you get thermal throttling at minute twelve, which
costs far more frame time over a session than the extra worker ever bought.

On Emscripten without `-pthread` it degrades to synchronous execution, so
calling code never needs a separate path.

## Web build

- **No Asyncify.** It costs 30–50 % throughput. The main loop is driven by
  `emscripten_set_main_loop` with `fps = 0`, which uses `requestAnimationFrame`
  — the only way to match the display and not burn battery.
- **`-sFILESYSTEM=0`.** Shaders are compiled into C headers at build time and
  embedded via `BGFX_EMBEDDED_SHADER`, so there is no asset-loading path to
  build for the web at all.
- **Threads need COOP/COEP headers**, which **GitHub Pages cannot set**. Host
  on Cloudflare Pages or Netlify.
- The canvas backing store is sized in *device* pixels, capped at DPR 2. Above
  that you are paying 3× the fragment cost for a difference nobody can see on a
  6-inch screen.

## Testing

101 tests, ~59 k assertions, all headless. Four things they are specifically
there to protect:

1. **The mesher's face accounting.** Every test asserts
   `mesh.faces == count_naive_faces(...)`. This is what caught the seam
   duplication bug.
2. **Gunfeel.** The active-reload state machine is pure logic and fully
   testable. Gunfeel is too important to be verifiable only by playing.
3. **The difficulty curve.** Budget monotonicity, roster caps, unlock
   scheduling, and determinism are all asserted, so a balance change that
   breaks the curve fails CI rather than shipping.
4. **The performance instrument itself.** The frame-time window, its
   percentiles, and the stall/dropped-step distinction are unit-tested, because
   a HUD that quietly reports the wrong number is worse than no HUD: it is the
   one number the whole milestone is judged on.

CI additionally runs the whole suite under ASan + UBSan. Undefined behaviour in
a voxel mesher is silent until it is a crash on someone's phone.

## Open questions

- **WebGPU vs WebGL2.** bgfx supports both. WebGL2 is the safe floor today;
  measure WebGPU on the target devices before switching.
- **Physics.** Jolt was the intended choice for M2 and has been dropped. The
  world is an axis-aligned uniform grid, so the character controller is a
  per-axis swept AABB over voxels — a few hundred lines in `game/physics.cpp`,
  headless and unit-tested. Jolt would have meant a third heavyweight
  dependency in `df_game`, which today links only glm and EnTT and must build
  for wasm and iOS, in exchange for solving a problem the grid solves for free.
  Revisit only if rigid-body debris or ragdolls become a requirement; neither
  is on the roadmap. Voxels still never get per-voxel bodies.
- **Audio.** miniaudio at M5. Nothing about the architecture depends on it yet.
- **Save format.** Sectors are bounded, so a whole-sector snapshot is viable.
  Prefer replaying the seed + input stream if determinism holds up.
