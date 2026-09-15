## Music update (0.1.1)

The suite now includes 23 audio/score unit tests (80 total) and four real-Web-Audio
browser scenarios (13 total). Run `python tests/browser_smoke.py --audio-only` for
just audio, or omit that flag for the full suite. `--memory --compatibility` remains
a restricted-environment diagnostic, not normal HTTP/CSP/WebGL validation. Audio
is real in either harness; webcam capture/inference are synthetic in these tests.
`tools/render-music-preview.py` renders the actual score with OfflineAudioContext.
See `docs/RIFT_RUNNERS_MUSIC.md` from the repository root for this update's evidence.

---

# Rift Runners tests

## Headless simulation, controls, camera ownership and replay validation

From `rift-runners/`, with Node 20 or newer:

```sh
npm run check
npm test
```

No package installation or build is needed. `npm test` discovers `tests/*.test.mjs`.
The simulation tests include a completed **generated** race, plus controlled fixtures
for collisions, shooting, gate scoring, and survival timing. The survival-timer
fixture deliberately removes hazards; it does not demonstrate winning a full swarm.
Camera unit tests use injected capture and worker implementations, not hardware.

## Real browser UI and input journeys

```sh
python -m pip install -r tests/requirements.txt
python -m playwright install chromium
PORT=4173 npm run serve
```

In another terminal, from the same directory:

```sh
python tests/browser_smoke.py --out qa-output
python tests/browser_smoke.py --compatibility --out qa-compatibility
python tests/browser_smoke.py --journey-only --out qa-terminal
```

Normal mode navigates to the real HTTP page and loads the shipped ES modules and
CSP. `--compatibility` explicitly disables WebGL to exercise the shared-scene
Canvas2D renderer. `--executable /path/to/chromium` selects an installed browser.
`--base-url` changes the default `http://127.0.0.1:4173/driftfall/` target.

Nine smoke scenarios cover the landing screen, keyboard input and restart,
permission denial, cancellation races, synthetic hand control and loss,
visibility cleanup, real touch events, invalid ghost files, and settings/sharing.
The camera/worker are **synthetic**: these tests do not load MediaPipe, measure
recognition accuracy, or represent a live webcam test. Browser errors fail the run.

The optional terminal journey drives the game with real keyboard events and a
3x test clock, without score, health or game-state setters. Its simple bot follows
gates but does not avoid obstacles. Either a real win or loss is acceptable; the
assertions check the result UI, correct ghost-export availability, and clean restart.
This is a state-transition test, not a proof of game balance or of a skilled win.

## Restricted-environment harness

`--memory` is explicitly a fallback for environments that prohibit page navigation.
It loads the **same game modules** as blob URLs, injects the same stylesheet and
removes CSP **only from the test document**. It does not edit the shipped HTML.
Worker URLs are redirected to a dummy URL for mocking. This mode is useful for UI,
input, resource-lifetime and compatibility-renderer checks, but it does **not**
validate deployed HTTP loading, CSP enforcement, remote model assets or WebGL.
The checkpoint-02 browser evidence was produced in this mode; consult the test report.

## Required live-device smoke test before wider release

Use an HTTPS deployment or localhost. Check that no permission prompt appears on
arrival. Play keyboard first. Then start hand mode, read the disclosure, consent,
allow video, hold the hand still until calibration is enabled, and complete a short
run. Test one hand, two hands, role swapping, occlusion, deliberate hand withdrawal,
Stop camera, hidden tab and camera disconnection. Confirm the browser camera
indicator goes out after stopping or hiding the tab. Check the network panel:
model/runtime assets should load only after opt-in; there should be no frame upload,
analytics or microphone requests. Record browser, device, lighting, inference time,
frame time and perceived control delay. Repeat on current Chrome, Safari and Firefox.

A vendor runtime or model load failure must leave keyboard/touch usable. Low frame
rates, role-label flicker, fatigue, tracking accuracy and accessibility still require
human tests; synthetic landmarks cannot establish them.
