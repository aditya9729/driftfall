# DRIFTFALL — Checkpoint 04 (Rift Runners 0.2.0)

Playtest fixes for the four issues reported against Checkpoint 03:
*"there is no collisions, where are the enemies? once calibrate just start.
two hand doesn't make sense."*

## 1. "Where are the enemies?"

The hostiles were being drawn, but drawn nearly invisibly. Both hulls used dark,
**non-emissive** colours — SEEKER `[.25,.15,.18]` and PYLON `[.31,.31,.24]`, both at
emission `0` — against a near-black sky (`[.035,.09,.13]`), then washed out by
exponential distance fog. Only a few small glowing slivers (the SEEKER eye, the PYLON
edge strips) were ever above the fog floor, which is why gates and cells read clearly
and hostiles did not.

- SEEKER hull is now hot red `[.93,.31,.26]` at emission `.75`, with a brighter eye
  and counter-rotating emissive fins.
- PYLON hull is now `[.66,.68,.52]` at emission `.45`, with brighter edge strips.
- Every hostile wears a four-box targeting bracket that brightens as it closes.
  Four boxes, not a ring: peak on-screen cost is +96 instances against the 7000
  instance cap, so the player ship — drawn after the entities — can never be
  starved of geometry.

## 2. "There is no collisions"

Two separate causes, both real.

**Hostiles were not in the way.** Placement scattered them `±5.5` units horizontally
around the gate line, so only **9.4%** of the 96 hostiles on `NEBULA-01` sat close
enough to the flight line to ever be a contact. A player threading gates could fly
most of a course without meeting anything. The spread is now `LANE_X 7.4 / LANE_Y 4.2`,
putting **28.1%** in the lane. This consumes the same rng draws in the same order.

Measured on `NEBULA-01`, flying the gate line:

| | before | after |
|---|---|---|
| hostiles in the lane | 9.4% | 28.1% |
| flying without firing | lost at 795/2100 | lost at 749/2100 |
| flying and firing | — | **won**, 40% hull, 65 kills, objective clear |

So the lane is now a gauntlet you shoot through, and ignoring it still kills you.

**A hit was silent.** Contact damage produced a 0.2s camera shake and nothing else —
no callout, no readout. The `damage` event now carries `amount` and `kind`, and the
HUD prints `✖ HULL HIT · -20 · 60% HULL`.

## 3. "Once calibrate just start"

Calibration required holding a hand steady and *then reaching for a button*, which is
the exact motion that breaks the pose being calibrated. A hand held steady for 1.4s
now calibrates and launches on its own, counting down in the calibration message.
The CALIBRATE & FLY button still works for anyone who prefers it.

## 4. "Two hand doesn't make sense"

One-hand was already the default. Two-hand is now labelled `(advanced)`, and the
"Swap two-hand roles" checkbox — previously always visible, and meaningless in
one-hand mode — is hidden unless two-hand is selected.

## Compatibility

`RULESET` 1 → 2. Course layout changed, so ghosts recorded on the old layout would
replay against a different course. They are now rejected with a clear message rather
than silently mis-replayed, and `GhostStore` keys are namespaced by ruleset, so old
saved ghosts are ignored rather than corrupting the new ones. **Personal-best ghosts
recorded before this checkpoint will not load.** This is intentional.

## Verification

- `node tools/check.mjs` — 10 modules, no external initial-page script.
- `node --test tests/*.test.mjs` — **93 passed, 0 failed** (88 before; 5 new in
  `tests/engagement.test.mjs` covering lane density, fatal-without-firing,
  winnable-with-firing, the damage payload, and v1 ghost rejection).
- Browser smoke over real HTTP, WebGL2: **13/13 PASS**.
- Browser smoke over real HTTP, Canvas2D compatibility: see report.

## Honest limitations

- **Still not validated on a real webcam.** Every hand test uses a synthetic
  capture/worker. The 1.4s auto-launch dwell in particular is tuned by reasoning, not
  by a human holding a hand in front of a camera — it needs a real pass, and the
  `WEBCAM_LIVE_CHECKLIST.md` steps still apply.
- Ghost challenges remain asynchronous replays, not live multiplayer.
- Balance numbers above come from a scripted gate-line pilot, not a human player.
  It is evidence that the lane is survivable-when-shot and fatal-when-ignored, not a
  claim about how the game feels to play.
- The GitHub Actions workflow still cannot create a WebGL2 backend on hosted runners,
  so deployment continues to go through the `gh-pages` branch.
