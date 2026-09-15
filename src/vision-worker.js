// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// Deliberately a CLASSIC worker: Emscripten's loader needs importScripts.
// Dynamic import gives us the ES module API without removing importScripts.
// This file is never executed until the player explicitly consents.
const ROOT = 'https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.14';
const MODEL = 'https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task';
const ALLOWED = new Set([
  `${ROOT}/vision_bundle.mjs`, `${ROOT}/wasm/vision_wasm_internal.js`,
  `${ROOT}/wasm/vision_wasm_internal.wasm`, `${ROOT}/wasm/vision_wasm_nosimd_internal.js`,
  `${ROOT}/wasm/vision_wasm_nosimd_internal.wasm`, MODEL
]);
function permit(url, method = 'GET') {
  const value = new URL(String(url), self.location.href).href;
  if (String(method).toUpperCase() !== 'GET' || !ALLOWED.has(value)) throw new Error('Non-asset network request blocked by DRIFTFALL privacy policy.');
  return value;
}
// Restrict fetch, XHR and importScripts to named GET assets, and block WebSocket.
// This is defense in depth, not an independent audit or sandbox of vendor code.
// The application itself has no frame-upload, analytics or recording pathway.
const nativeFetch = self.fetch.bind(self);
self.fetch = (resource, init = {}) => {
  const isRequest = typeof Request !== 'undefined' && resource instanceof Request;
  const url = permit(isRequest ? resource.url : resource, init.method || (isRequest ? resource.method : 'GET'));
  if (init.body != null) return Promise.reject(new Error('Request bodies are prohibited.'));
  return nativeFetch(url, { ...init, method: 'GET', body: undefined, credentials: 'omit', referrerPolicy: 'no-referrer' });
};
const nativeImportScripts = self.importScripts.bind(self);
self.importScripts = (...urls) => nativeImportScripts(...urls.map(url => permit(url)));
if (typeof XMLHttpRequest !== 'undefined') {
  const NativeXHR = XMLHttpRequest;
  self.XMLHttpRequest = class extends NativeXHR {
    open(method, url, ...rest) { return super.open('GET', permit(url, method), ...rest); }
    send(body = null) { if (body != null) throw new Error('Request bodies are prohibited.'); this.withCredentials = false; return super.send(null); }
  };
}
self.WebSocket = class { constructor() { throw new Error('WebSocket is disabled in the tracking worker.'); } };
let detector = null;
self.onmessage = async ({ data }) => {
  if (data.type === 'init') {
    try {
      const { FilesetResolver, HandLandmarker } = await import(`${ROOT}/vision_bundle.mjs`);
      const vision = await FilesetResolver.forVisionTasks(`${ROOT}/wasm`);
      detector = await HandLandmarker.createFromOptions(vision, {
        baseOptions: { modelAssetPath: MODEL, delegate: 'CPU' },
        runningMode: 'VIDEO', numHands: 2, minHandDetectionConfidence: .6,
        minHandPresenceConfidence: .6, minTrackingConfidence: .6
      });
      self.postMessage({ type: 'ready' });
    } catch (error) { self.postMessage({ type: 'error', message: `Hand tracking could not load: ${error?.message || error}` }); }
  } else if (data.type === 'frame') {
    try {
      if (!detector) throw new Error('Tracking model is not ready.');
      const start = performance.now();
      const result = detector.detectForVideo(data.bitmap, data.timestamp);
      self.postMessage({ type: 'result', landmarks: result.landmarks, handedness: result.handedness,
        timestamp: data.timestamp, inferenceMs: performance.now() - start });
    } catch (error) { self.postMessage({ type: 'error', message: `Hand tracking stopped: ${error?.message || error}` }); }
    finally { data.bitmap?.close(); }
  }
};
