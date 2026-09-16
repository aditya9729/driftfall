# DRIFTFALL — Checkpoint 06 (Rift Runners 0.4.0)

Answers: *"what about boosting? what about hand gesture control being smoother?
what about a global leaderboard?"* plus *"fist should lead to shooting"*.

## 1. Boosting

Two separate problems, one of them a real bug.

**One-hand mode had no boost gesture at all.** `gestures.js` read:

```js
boost: this.calibrated && this.mode === 'two' && this.boostPinch.update(pilot.pinch, now)
```

Since one hand is the default control style, hand players could only boost with
Shift on a keyboard — the same shape of gap as the fire fallback in checkpoint
05. There is now a **throttle: push your hand toward the camera.** The knuckle
span widens with proximity, so it needs no new landmarks and cannot collide
with a pinch or a fist. Calibration captures your neutral depth as the
baseline; engage at 1.22x with a 90 ms dwell, release at 1.10x.

**Boosting was also pointless** — pure speed, no payoff, so there was no reason
to spend energy except at the finish line. A gate taken while boosting now pays
**1.5x**, and the energy curve is slightly more generous (-26/s burn, +16/s
regen, was -29/+13). Measured on `NEBULA-01` down the gate line:

| strategy | result | time | score |
|---|---|---|---|
| hold boost the whole way | **lost** at 1761/2100 | — | — |
| never boost | won | 75.4s | 57,050 |
| **managed** (spend, don't run dry) | won | **52.2s** | **63,825** |

Managed boosting now beats both extremes, and holding it down is fatal. Two
existing fixtures flew with `boost: true` pinned on and began failing; their
**flying** was corrected, not their assertions.

## 2. Smoother hand control — and a filter I got wrong first

The mapper used one fixed exponential (tau 60 ms), which must trade jitter at
rest against lag in motion. It is now a **One Euro filter**, whose cutoff widens
with hand speed.

**The first attempt was worse, and nearly shipped.** One Euro's `beta`
multiplies the signal's own derivative, so it is *signal-scale dependent*. The
literature's `0.035` suits pixel coordinates; on a signal normalised to ±1 it is
~40x too small, so the cutoff never widened and the filter sat at its slow floor:

| filter | step to 90% | tremor at rest | tracking lag |
|---|---|---|---|
| old fixed tau=60ms | 0.167s | 0.0033 | 0.0050 |
| first attempt (beta 0.035) | **0.367s** | 0.0012 | **0.0317** |
| shipped (beta 1.5) | **0.100s** | **0.0013** | **0.0043** |

The first attempt was 2.2x slower to respond and tracked 6x worse, while
*looking* like an improvement because tremor fell. All 119 unit tests passed
throughout — including a smoothing test that only checked the half of the
tradeoff that had improved. The browser smoke caught it: the synthetic-hand
journey reported `player.x = 0.81` where it expected more travel.

Shipped settings beat the old filter on **every** metric. Aim uses `beta 0.2`
because it spans ±16 rather than ±1. A regression test now asserts the filter is
never laggier than the one it replaced.

## 3. Fist to fire

`describeHand` gained **curl**: mean fingertip-to-own-knuckle distance over
knuckle span. Fingers out reads ~1.1, a closed fist ~0.45. Normalising by span
keeps it independent of camera distance, so it cannot be read as a throttle.

The subtlety: **a real fist usually squeezes thumb and index together too**, so
it already tripped the pinch threshold intermittently — firing unpredictably
rather than not at all. Both detectors now run every frame and fire is
`pinching || fisted`. The detector calls are deliberately not short-circuited:
an un-updated detector keeps stale dwell state and misfires the instant the
other releases.

Space, click and touch FIRE all still work, as does pinch.

## 4. Global leaderboard (Cloudflare Workers + D1)

Built, **not yet live** — it needs a Cloudflare account this machine does not
have. See `leaderboard/README.md`.

**What it verifies.** The Worker imports the *game's own* `validateReplay`, so
server and client rules cannot drift. A submission is rejected unless it is a
well-formed ghost: monotonic time, no sample over the speed limit, the route
finished, and a finish above the physical floor `COURSE_LENGTH / (48 * 1.08)`.
Nobody can post a 3-second lap, a half-run, a teleporting ship or a typed
number, and every entry carries a downloadable ghost.

**What it does not.** The run is not re-simulated from inputs, so a determined
person could craft a physically-plausible ghost by hand. Swarm *score* is
claimed, not recomputed, because position ghosts record no kills or combo — so
Rift Run ranks by **time**, the constrained quantity, and the API says so in its
own response. Closing that gap means recording inputs so the server can re-run
the deterministic simulation; a real option, and a larger change.

Writes are origin-locked, rate limited 6/min, and rate limiting stores a salted
truncated hash for 60 seconds — never an address. The in-game board hides itself
entirely when unconfigured rather than showing dead controls.

## 5. A bug the leaderboard tests found

`cleanFlight` used `Number(v)`, and **`Number(null)` is `0`, not `NaN`**. Since
`URLSearchParams.get()` returns `null` for an absent key, any share link that
set only *some* flight parameters silently clamped density and spread to the
**0.5x minimum** — handing your friend a different course under the same seed.
Shipped in checkpoint 05, fixed here, pinned by a regression test.

`RULESET` 3 → 4 (boost scoring changes run scores).

## Verification

- `node --test tests/*.test.mjs` — **126 passed, 0 failed** (102 before).
- Browser smoke over real HTTP, one suite at a time: **13/13 WebGL2 + 13/13
  Canvas2D**.

## Honest limitations

- **Gesture thresholds are unvalidated on real hardware.** Fist engage/release
  (0.62/0.78) and throttle engage/release (1.22/1.10) come from hand geometry,
  not from watching a real hand on a real camera. The synthetic tests prove the
  logic — dwell, hysteresis, no stuck states — not the numbers. These are the
  dials if a fist does not register, or registers when you merely relax.
- Pilot balance remains bot-measured, and that bot never manages heat.
- The leaderboard is unproven against real D1; it has only run against a
  test double of the database.
