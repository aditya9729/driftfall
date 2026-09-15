# Rift Runners checkpoint 02 — validation report

Date: 2026-09-15. Game version: 0.1.0. New additive browser client only.
No native C++ code was changed or compiled/tested in this session. No remote push,
GitHub Actions run or Pages deployment was completed.

## Executed checks

| Layer | Result | Scope |
|---|---|---|
| JavaScript syntax and initial-page check | Pass | All 9 source modules; no external initial-page script tag |
| Node unit tests | 57 passed, 0 failed | Simulation, collision, input mapping, camera lifetime, replay validation and worker request guards |
| Chromium smoke scenarios | 9 passed, 0 failed | Actual UI/input events in the in-memory harness; Canvas2D renderer; synthetic camera/worker |
| Terminal game/restart journey | Pass | Real key events, explicitly accelerated 3x test clock; genuine game-over and clean restart |
| Local HTTP asset checks | 26 passed | 13 assets at both root and `/driftfall/`; MIME and byte equality; missing asset gives 404 |
| Screenshots | Captured and inspected | Desktop/mobile menu and flight, consent panel, synthetic hand flight, terminal screen |

Node version: 22.16.0. Python: 3.13. Playwright: 1.57.0. Browser: installed Chromium
at `/usr/bin/chromium`, headless, Canvas2D compatibility path explicitly selected.
There was no actual webcam or external model inference in the test environment.

The unit suite includes a fully generated `NEBULA-01` race completed using an
ordinary input-controller loop, without setting health, score or terminal state.
The 90-second survival timing test uses a deliberately collision-free fixture.
That timing test is not evidence of winning a normal generated Swarm Run.

The optional browser pilot follows gates using real key events but does not avoid
hazards. It reached **a loss**, not a win, at approximately 49.19 simulated seconds
and 1,859.83 route units, then exercised result/restart. An earlier exploratory
version of that test demanded a win and failed when the bot died; the final test
explicitly measures correct terminal transitions for either outcome. No gameplay
health, damage or outcome was altered to make this test pass. Human balance and
playability claims are not inferred from this bot.

## Nine browser scenarios

1. Landing renders with no camera call, no active camera and no model-host requests.
2. Keyboard steering, firing, boost, pause/resume, exit and clean restart work.
3. Unchecked consent prevents capture; explicit denial gives a usable fallback.
4. Cancelled pending permission releases a late-arriving stream; consent resets.
5. Synthetic landmarks calibrate, steer and fire; missing hands pause; Stop releases
   the stream/worker; keyboard fallback resumes.
6. Hiding a tab pauses and releases the synthetic camera.
7. The mobile layout fits; actual browser touch events steer and fire.
8. A malformed imported ghost is rejected without breaking the menu.
9. Mode selection, UTC daily course, reduced motion and course-sharing UI work.

Camera tests substitute capture and landmark inference. They exercise our ownership,
UI and mapping code, not Google's model, a physical permission prompt or real hands.
All unexpected `pageerror` events fail the corresponding smoke scenario.

## Environment constraints — important

Normal Chromium navigation is blocked by managed environment policy, even to the
local server, and WebGL context creation failed in this environment. Those policies
were not changed or bypassed. Instead, a documented `--memory` harness loaded the
same source modules as blob URLs into an allowed `about:blank` document and injected
the actual stylesheet. It removed CSP from the **test document only**; the shipped
HTML retains CSP. Worker URLs were redirected solely to support synthetic tests.

Consequently the browser evidence does **not** establish real HTTP module loading,
CSP correctness in a deployed browser, remote runtime downloads, webcam inference,
or the WebGL shader/instancing backend. Separate ordinary HTTP checks established
that the local server returns the exact shipped assets at both hosting prefixes,
but they do not close those browser-validation gaps.

The provided GitHub workflow runs browser smoke tests in normal HTTP mode with the
actual shipped CSP. The landing check asserts the expected renderer backend, so a
WebGL test cannot silently pass by taking the Canvas2D fallback. Its execution, any resulting errors and deployment must still
be checked after the owner pushes. Hardware rendering and live-camera tests remain
release gates even when synthetic CI passes.

## Issues found and addressed before checkpoint 02

- Added a real Canvas2D 3D compatibility renderer when WebGL2 cannot be created.
  It projects the same scene instances and camera; it is not a static preview.
- Software-composited backdrop blur plus a continuously rendered menu reduced
  synthetic tracking to roughly 3–4 samples/second, preventing stable calibration.
  Calibration now throttles the covered background and disables live blur in the
  compatibility renderer. The same tests subsequently passed without weakening
  freshness thresholds or replacing live input with stale coordinates.
- Guarded camera generation tokens, pending startup cancellation and late stream
  resolution; unit/browser tests verify resources are released.
- Stale hand controls release firing before the longer auto-pause interval.
- Corrected aim unprojection, deterministic replay serialization of negative zero,
  and bounded local ghost retention.
- Fixed hand-mode retry/consent state transitions and cleared held input on pause.
- Replaced reversed smoothstep ranges in shader source; the shader backend itself
  still needs real WebGL validation.

## Not implemented or not verified

No live multiplayer, backend, matchmaking, anti-cheat or verified online scores.
No integration with the original native simulation. No native C++ regression run.
No hardware-level camera, model download, tracking accuracy or demographic coverage
test. No measured input-to-photon latency, GPU performance budget, thermal soak,
long-session fatigue study or mobile Safari/Firefox validation. No independent
third-party runtime/security audit. No claim of production/commercial readiness.

## Reproducing and reviewing evidence

Run `npm run check` and `npm test` in `rift-runners/`.
See `rift-runners/tests/README.md` for normal HTTP browser tests, compatibility mode,
the restricted harness and the live-device checklist.
The QA ZIP contains the raw final unit output, smoke/terminal JSON reports,
terminal state, HTTP check report and current screenshots. It does not bundle
camera images or hand recordings; the hand test uses synthetic coordinates.
