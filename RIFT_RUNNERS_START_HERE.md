> **Music update / browser version 0.1.1:** This overlay now includes the original
> Neon Wake score, SOUND/MIX controls, independent volume sliders and an M mute key.
> See `docs/RIFT_RUNNERS_MUSIC.md` for current changes, 80 unit tests / 13 browser
> scenarios and their limitations. The Checkpoint 02 guide below is retained as
> baseline integration context; no deployment or real-webcam validation is implied.

# DRIFTFALL / Rift Runners — checkpoint 02

A playable, additive browser prototype: race through a fractured station, thread
scoring gates, blast machine drones and obstacles, manage boost and weapon heat,
and challenge local or file-shared ghosts. This checkpoint supersedes checkpoint 01.

## What this package is — and is not

This is an **additive repository overlay, not a complete repository clone**. Extract
its contents into your existing `aditya9729/driftfall` working tree. It adds
`rift-runners/`, new documents and a dedicated Pages workflow. No existing C++ source,
CMake file, native test, existing web shell or original workflow is replaced.

The new browser game is an independent prototype, **not a port or integration of the
C++ simulation**. There is no real-time multiplayer, server, matchmaking, cloud score
service or verified leaderboard. Ghost challenges are asynchronous and client-side.

Nothing was committed, pushed or deployed from this session. The GitHub connection
could read the repository but its branch-write request was denied earlier. You retain
control of the push and publication. Audited base: commit
`0525c31bc53f5494d36bde927a614b3dbfe92cef`; default branch at that snapshot:
`claude/code-tool-approval-issue-ll0ku0`. Do not overwrite newer work blindly.

## Run locally

From your repository root:

```sh
cd rift-runners
npm run check
npm test
npm run serve
```

Open `http://localhost:8080/driftfall/`. Use `PORT=4173 npm run serve` when port 8080
is busy. The game needs Node 20+ for the development server/tests, but no npm install
and no build step. The deployed site needs only a browser. Do not double-click
`index.html`: ES module loading and camera mode need a suitable origin.

Choose **KEYBOARD / TOUCH** to start without a webcam or model download. WASD/arrows
steer, Space or mouse press fires, Shift boosts, P/Escape pauses. On touch screens,
drag the play area to steer and use FIRE/BOOST. Sound is off until enabled.

Hand mode requires a fresh consent checkbox, camera permission and calibration.
One hand steers and pinches to fire. Two hands split piloting/boost and aiming/fire;
roles can be swapped. R recenters after the hands are held still. Stop camera is
available during play/pause; exiting or hiding the tab also releases the stream.

## Push safely and deploy the source overlay

Extract the **source ZIP** into the existing clone, not into another nested
`driftfall/` folder. The root of the ZIP contains `rift-runners/`, `docs/`, and
`.github/workflows/` — include the hidden `.github` directory.

A reviewable branch is preferable:

```sh
git switch -c feature/rift-runners
# Extract the source ZIP into this repository directory now.
git status --short
git add rift-runners docs/RIFT_RUNNERS_DESIGN.md docs/RIFT_RUNNERS_TEST_REPORT.md \
  .github/workflows/rift-runners-pages.yml RIFT_RUNNERS_START_HERE.md
git diff --cached --stat
git commit -m "Add consent-first Rift Runners browser prototype"
git push -u origin feature/rift-runners
```

Open a pull request and merge after review into `main` or the audited default branch.
The workflow runs on pull requests and on pushes to those two named branches. A
manual workflow dispatch is also available; update the branch list deliberately if
your repository's default branch has since changed.

Set **Settings → Pages → Build and deployment → Source → GitHub Actions**.
The **Rift Runners — test and deploy** workflow runs syntax checks, 57 unit tests and
browser smoke tests, stages the static client, then publishes it. Browser checks in
CI use normal HTTP mode, with a normal renderer run and a compatibility run. That
workflow is provided but has not been executed on GitHub from this session.

The intended URL after a successful deployment is
`https://aditya9729.github.io/driftfall/`. It is **not a verified live deployment**.
GitHub's `github-pages` environment must permit your publishing branch. Resolve a
specific branch restriction deliberately; do not disable unrelated protection rules.

## Alternative: static-site ZIP

`driftfall-checkpoint-02-github-pages.zip` contains `index.html` and its assets at the
ZIP root, with no build tools. Use it for a dedicated static host or the root of a
separate `gh-pages` branch. For branch publishing, select that branch and `/(root)`
in Pages settings. Do **not** extract it over your main C++ repository root as a
substitute for the source overlay. It does not include the source overlay's workflow.

## Validation and remaining gates

- 57/57 Node unit tests passed, including a completed generated race.
- Nine Chromium smoke scenarios passed using the Canvas2D compatibility renderer,
  plus a terminal-game/restart journey. These browser tests used the explicitly
  documented in-memory harness because ordinary navigation is blocked here.
- 26 HTTP asset/MIME/byte-equality checks passed through the real local server.
- Desktop, mobile, in-flight and camera-consent screenshots are in the QA ZIP.

**Not established here:** live webcam/model operation or recognition accuracy;
WebGL2 rendering; deployed browser CSP/HTTP behavior; GitHub Actions execution;
mobile Safari/Firefox compatibility; real-device frame rate, latency or fatigue;
production readiness. The compatibility renderer is a genuine dynamic 3D projection,
not a replacement screenshot. Review `docs/RIFT_RUNNERS_TEST_REPORT.md` before release.

## Privacy and network behavior

No camera on arrival. Video only after consent; no microphone, app analytics, account,
recording or frame-upload code. Camera frames/landmarks are not saved to ghost files.
The optional hand mode downloads pinned MediaPipe 0.10.14 runtime/WASM from jsDelivr
and a hand model from Google's model host. The disclosure explains those third-party
requests and their ordinary network metadata. Worker request guards restrict fetch,
XHR and importScripts to named GET assets and block WebSocket, but are not a vendor
security audit. Camera mode therefore is not an offline-first or dependency-free mode.
Keyboard/touch gameplay has no remote runtime dependency.

## Handoff map

- `docs/RIFT_RUNNERS_DESIGN.md`: game design, architecture decision, task sequence,
  privacy contract and staged multiplayer plan.
- `docs/RIFT_RUNNERS_TEST_REPORT.md`: what passed, what was not tested, limitations.
- `rift-runners/src/`: simulation, renderers, UI, hand controls, worker and ghosts.
- `rift-runners/tests/README.md`: repeatable Node/browser tests and live-device checklist.
- `.github/workflows/rift-runners-pages.yml`: test/package/publish workflow.

The highest-value next milestone is a short real-webcam playtest of one-hand and
split-hand controls, followed by latency/fatigue tuning. The design document places
real-time networking after that validation, not behind a misleading multiplayer button.
