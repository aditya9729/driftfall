# DRIFTFALL — Checkpoint 05 (Rift Runners 0.3.0)

Answers three reports: *"where are the ghosts — how can I fire? what happened to
my requests for custom flight"*.

## 1. "How can I fire?"

A real bug, and a bad one. In hand-tracking mode the input path was:

```js
if (inputMode === 'hands') return { ...mapper.input(now),
  boost: mapper.input(now).boost || keys.has('ShiftLeft') || ... };
```

**Boost** fell back to the keyboard. **Fire did not.** So in hand mode the pinch
gesture was the only way to shoot, and if pinch detection struggled — bad light,
hand angle, distance — there was no way to fire at all. The help text already told
you Space would work; the code never honoured it.

Space, click and the touch FIRE button now fire in hand mode, exactly as they do
for boost. The control strip says `PINCH OR SPACE · FIRE`.

## 2. "Where are the ghosts?"

Ghosts were real, but almost unreachable:

```js
lastReplay = run.status === 'won' ? record(run) : null;
```

**A ghost was only ever created if you completed the entire 2,100-unit course.**
Until then no ghost existed, nothing was stored, and the HUD read `SOLO FLIGHT`
permanently. Combined with hostiles you couldn't see (checkpoint 04), essentially
nobody reached the one condition that creates the feature. The Checkpoint 04
ruleset bump then invalidated anything older.

Now every finished flight leaves a ghost: a win becomes a **personal best**, a
crash becomes a **best attempt** keyed on how far you got. `isBetter` ranks a
completed run above any attempt, and a further attempt above a shorter one. The
menu hint reads `NO GHOST YET · YOUR FIRST RUN MAKES ONE`, then
`BEST ATTEMPT 1240 M · GHOST READY`.

**The integrity rule is unchanged where it matters.** `validateReplay` gained a
`{ local }` flag. Local storage accepts an unfinished attempt; an imported *file*
from another person must still be a genuine completed run, and SAVE GHOST stays
disabled unless you finished. Local convenience never weakened what a shared file
may claim.

## 3. "What happened to my requests for custom flight"

Built, and wired into the simulation rather than bolted on. **BUILD YOUR FLIGHT**
in the flight deck opens a loadout dialog:

- **Pilot** — VESPER (balanced), KITE (fast, thin hull), BASTION (heavy, slow turn,
  cool barrel), EMBER (rapid fire, hot barrel). These change real simulation values:
  speed, agility, `maxHealth`, weapon cooldown and heat gain. The HUD hull meter is
  now a percentage of *your* pilot's hull, not a hard 100. Your ship takes the
  pilot's tint.
- **Hostiles** — balanced / seekers only / pylons only. Choosing which enemies a
  course fields was the "select enemies per level" ask.
- **Density** 0.5×–2× and **Spread** 0.5×–1.6×.
- **View** — chase cam or **cockpit (first person)**, which moves the eye to the
  hull, widens the FOV to 74° and stops drawing your own ship.
- **Theme** — auto (shifts as you fly) or pin The Fracture / Violet Wake / Sunken
  Sun. The HUD zone label follows the pinned choice.

A live readout shows what you are about to fly: *"137 hostiles across 11 waves ·
objective 40 · hull 90 · custom course, separate ghosts."* The loadout persists
across reloads and rides along in share links.

**Ghost integrity across builds.** A custom flight is a genuinely different course
under the same seed, so ghosts are keyed by a `flightTag(flight, pilot)` and carry
their build in the payload. You can never race a ghost flown on different settings.
VESPER's multipliers are all exactly 1 and the default flight generates a
byte-identical course, so standard runs are unchanged.

`RULESET` 2 → 3 (the ghost payload gained pilot and flight identity).

## Verification

- `node tools/check.mjs` — 10 modules, no external initial-page script.
- `node --test tests/*.test.mjs` — **102 passed, 0 failed** (93 before; 9 new in
  `tests/loadout.test.mjs`).
- Browser smoke over real HTTP: **13/13 WebGL2 + 13/13 Canvas2D**, 0 failures.
  Canvas2D is a software renderer and is sensitive to machine load: running two
  suites at once starves the fixed-step loop and fails timing assertions on
  checkpoint 04 code as readily as on this work. Both versions were re-measured
  one suite at a time and both pass 13/13.
- Ghost fix driven end-to-end in a browser: menu reads `NO GHOST YET`, a crashed
  run stores `driftfall.ghost.v3.race.STD.NEBULA-01` and reports
  `BEST ATTEMPT SAVED · 980 M`, SAVE GHOST stays disabled on the loss, the menu
  then reads `BEST ATTEMPT 978 M · GHOST READY`, and the next run's HUD shows
  `0 M BEHIND GHOST` instead of `SOLO FLIGHT`.
- All 24 pilot × mix × mode combinations simulated to a terminal state; none
  crashed and every hull bound held.
- Flight builder driven in a real browser: pilot select, mix, density, cockpit view
  and theme all applied, zero page errors.

## Honest limitations

- **Still no real-webcam validation.** The fire fallback matters most exactly where
  I cannot test: if pinch is unreliable on your hardware, Space now saves the run,
  but I have not watched a real hand fail to pinch and then recover.
- Pilot balance is checked for *playability*, not tuned against human play. A first
  pass had EMBER losing every race and BASTION winning every one, so EMBER's heat
  gain came down (1.22 → 1.08) and BASTION's hull and cooling were trimmed
  (1.35 → 1.25, .8 → .85). Scripted race wins are now VESPER 2/3, KITE 1/3,
  BASTION 3/3, EMBER 2/3 — but that bot holds the trigger permanently and never
  manages heat, so it understates any pilot whose weakness is overheating. Treat
  these as playability evidence, not balance.
- Density above ~1.5× with `spread` low makes some courses very punishing. It is
  allowed on purpose, but it is not balanced.
- Ghost races remain asynchronous replays, not live multiplayer.
