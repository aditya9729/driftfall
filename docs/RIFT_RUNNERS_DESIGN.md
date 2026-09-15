# DRIFTFALL: Rift Runners — design v0.1

## Experience
Your hands are the cockpit. A 60–90 second, third-person arcade flight through a fractured space station: steer through gates, shoot obstructing blocks and machine drones, spend energy to boost, and race the trace of your previous best flight. One clear interaction before a platform full of modes.

The intended feel is fluid rather than frantic: small wrist movements, generous openings, forgiving aim, immediate effects, and rapid restart. This is a hypothesis for human playtests, not a claim of measured engagement.

## First release
- A standalone browser client under `rift-runners/`, with a dependency-free WebGL2 renderer and a shared-scene 3D Canvas2D compatibility renderer. MediaPipe is an optional remote dependency for camera input only.
- Three connected visual biomes: The Fracture, Violet Wake, and Sunken Sun.
- Rift Run (finish the route), Swarm Run (survive 90 seconds), Daily Rift (UTC-seeded course).
- Keyboard, pointer and touch without camera permission or external runtime downloads.
- Opt-in webcam input using MediaPipe Hand Landmarker in a worker; one-hand and two-hand modes, calibration, smoothing, pinch hysteresis, and stale-input protection.
- Local personal best ghosts, explicit ghost-file import/export, and shareable course links. These are asynchronous challenges, NOT live multiplayer or a verified leaderboard.
- An implemented launch → consent/calibration or fallback → countdown → run → result → replay loop. See the test report for which paths have actually been exercised.

## Repository decision
The existing C++20/WebAssembly voxel game is preserved. This release is an **additive browser experiment, not a port or integration of its simulation**. All existing files remain untouched. The existing threaded WASM renderer and build-then-defend game remain a separate product path. The browser experiment tests the hand-flight loop without needing cross-origin isolation or a C++ build. Select the long-term shared simulation after validating this loop with players.

## Controls
One hand: move the palm to steer; thumb/index pinch to fire along the aiming marker. Two hands: the initial screen-left hand pilots and pinches to boost; the screen-right hand aims and pinches to fire. Swap roles is available. Calibrate in a comfortable neutral pose; support elbows and use short sessions.

Keyboard: WASD/arrows steer; Space or pointer press fires; Shift boosts; P/Escape pauses; R recenters camera controls; F3 shows diagnostics. Touch: drag the play area to steer; explicit FIRE and BOOST buttons. No forced pointer lock.

## Safety and privacy contract
Never call getUserMedia on load or from a saved preference. A fresh explicit acknowledgement and camera button are required. Request video only, never microphone. Camera frames and landmarks remain in browser memory and are never included in replays. No analytics, accounts, biometric identification, backend or video recording. Model/runtime downloads contact jsDelivr and Google's model host after consent, revealing normal network metadata such as IP address to those providers. Show camera status and an immediate Stop camera action. Release all tracks on cancellation, late-resolving permission, runtime failure, exit, page hide, or hidden tab. A missing hand releases fire immediately and sustained tracking loss pauses instead of auto-resuming into danger.

## Implementation tasks and acceptance
1. Pure seeded simulation, swept collisions, gates, energy/heat, and reproducible runs.
2. Instanced 3D renderer with bounded geometry, procedural scenery and game-state-aligned collisions.
3. Complete accessible UI, touch fallback, audio opt-in, reduced motion, and privacy flow.
4. Worker-based hand tracking and a testable landmark-to-input mapper.
5. Bounded, validated local ghosts and course sharing without a server.
6. Node unit tests; Chromium journeys, denial/cancel/cleanup tests; screenshots; package and deploy workflow.

## Next milestones, not part of v0.1
- Human testing across webcams, handedness, skin tones, lighting and motor-access needs; measure end-to-end input latency and fatigue before tuning gestures.
- Shared C++ simulation/JS bridge evaluation rather than permanent duplicate gameplay implementations.
- Real multiplayer: authoritative rooms on a separate WebSocket server, interpolation, reconciliation, rate limiting, abuse handling and privacy review. GitHub Pages hosts the client, not that service. Never send webcam frames to multiplayer peers.
- Skill-balanced matchmaking, accessible one-handed controls, spectator replay camera, richer authored enemies and a boss, collaborative races, replay-driven course editor.

## Technical references
- Experience-first games and observable tests: https://developers.openai.com/blog/how-to-build-games-with-astra
- MediaPipe hand landmarks and worker recommendation: https://ai.google.dev/edge/mediapipe/solutions/vision/hand_landmarker/web_js
- Camera permission and secure contexts: https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getUserMedia
- GitHub Pages custom deployment: https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages

## Checkpoint 02 implementation status

Tasks 1–6 have code and reproducible tests. The browser prototype is playable with
keyboard and touch; synthetic hand integration has passed browser tests. The actual
MediaPipe download/inference path and WebGL backend still need live-browser/device
validation outside the restricted implementation environment. Real-time multiplayer,
C++ integration and commercial release readiness are not claimed. The current Pages
workflow is included for the owner to push; no remote deployment was performed here.

A software-compositor regression found during testing was fixed: calibration dialogs
now throttle the covered background scene, and the compatibility renderer does not
apply live backdrop blur. This preserves capture time rather than relaxing the
freshness requirement to accept stale hand positions.
