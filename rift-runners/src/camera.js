// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// Owns every camera resource. Generation tokens protect cancellation races.
export function stopTracks(stream) { for (const track of stream?.getTracks?.() || []) track.stop(); }
export function cameraMessage(error) {
  if (error?.name === 'NotAllowedError') return 'Camera permission was denied. You can play with keyboard or touch, or enable camera access in your browser and try again.';
  if (error?.name === 'NotFoundError') return 'No camera was found. Keyboard and touch are ready to use.';
  if (error?.name === 'NotReadableError') return 'Your camera may be in use by another app. Close it there or play without a camera.';
  return error?.message || 'Camera setup failed. You can play without a camera.';
}
export class CameraSession {
  constructor({ video, onStatus = () => {}, onResult = () => {}, onError = () => {}, onStopped = () => {}, env = {} }) {
    this.video = video; this.onStatus = onStatus; this.onResult = onResult; this.onError = onError; this.onStopped = onStopped;
    this.env = {
      secure: () => globalThis.isSecureContext,
      capture: constraints => navigator.mediaDevices.getUserMedia(constraints),
      supported: () => Boolean(navigator.mediaDevices?.getUserMedia && globalThis.Worker && globalThis.createImageBitmap),
      worker: () => new Worker(new URL('./vision-worker.js', import.meta.url)),
      bitmap: video => createImageBitmap(video),
      raf: fn => requestAnimationFrame(fn), cancelRaf: id => cancelAnimationFrame(id),
      now: () => performance.now(), ...env
    };
    this.generation = 0; this.stream = null; this.worker = null; this.frameId = null;
    this.active = false; this.ready = false; this.busy = false; this.lastFrameAt = -Infinity; this.lastVideoTime = -1;
    this.initTimer = null; this.pendingReject = null; this.captureConstraints = null;
  }
  async start({ consent = false } = {}) {
    if (consent !== true) throw new Error('Explicit consent is required before camera access.');
    if (!this.env.secure()) throw new Error('Camera mode needs HTTPS or localhost. Keyboard play is still available.');
    if (!this.env.supported()) throw new Error('This browser does not support camera tracking. Try a current desktop browser or play with keyboard/touch.');
    this.stop(); const generation = this.generation;
    this.onStatus('permission');
    try {
      this.captureConstraints = { audio: false, video: { facingMode: 'user', width: { ideal: 640 }, height: { ideal: 480 }, frameRate: { ideal: 24, max: 30 } } };
      const stream = await this.env.capture(this.captureConstraints);
      if (generation !== this.generation) { stopTracks(stream); return false; }
      this.stream = stream; this.active = true;
      for (const t of stream.getVideoTracks()) t.addEventListener?.('ended', () => {
        if (generation === this.generation && this.active) this.fail(new Error('The camera was disconnected. Continue with keyboard or reconnect it.'));
      }, { once: true });
      this.video.srcObject = stream; await this.video.play();
      if (generation !== this.generation) return false;
      this.onStatus('loading');
      const worker = this.env.worker(); this.worker = worker;
      await new Promise((resolve, reject) => {
        this.pendingReject = reject;
        this.initTimer = setTimeout(() => reject(new Error('The hand model did not load. Check your connection or content blocker, or use keyboard/touch.')), 30000);
        worker.onmessage = ({ data }) => {
          if (generation !== this.generation) return;
          if (data.type === 'ready') {
            clearTimeout(this.initTimer); this.initTimer = null; this.pendingReject = null;
            this.ready = true; resolve();
          } else if (data.type === 'error') {
            const error = new Error(data.message);
            if (!this.ready) reject(error); else this.fail(error);
          } else if (data.type === 'result') {
            this.busy = false; this.onResult(data, this.env.now());
          }
        };
        worker.onerror = event => {
          const error = new Error(event.message || 'The tracking worker could not start.');
          if (!this.ready) reject(error); else this.fail(error);
        };
        worker.postMessage({ type: 'init' });
      });
      if (generation !== this.generation) return false;
      this.onStatus('ready'); this.schedule(generation); return true;
    } catch (error) {
      if (generation !== this.generation) return false;
      this.stop(); this.onError(error); return false;
    }
  }
  schedule(generation) {
    const loop = now => {
      if (generation !== this.generation || !this.active || !this.ready) return;
      this.frameId = this.env.raf(loop);
      if (this.busy && now - this.lastFrameAt > 4000) { this.fail(new Error('Hand tracking stopped responding. The camera has been turned off.')); return; }
      if (this.busy || now - this.lastFrameAt < 50 || this.video.readyState < 2 || this.video.currentTime === this.lastVideoTime) return;
      this.busy = true; this.lastFrameAt = now; this.lastVideoTime = this.video.currentTime;
      this.env.bitmap(this.video).then(bitmap => {
        if (generation !== this.generation || !this.worker) { bitmap.close(); return; }
        this.worker.postMessage({ type: 'frame', bitmap, timestamp: this.env.now() }, [bitmap]);
      }).catch(error => { if (generation === this.generation) this.fail(error); });
    };
    this.frameId = this.env.raf(loop);
  }
  fail(error) { this.stop(); this.onError(error); }
  stop() {
    this.generation++;
    const reject = this.pendingReject; this.pendingReject = null;
    clearTimeout(this.initTimer); this.initTimer = null;
    if (this.frameId !== null) this.env.cancelRaf(this.frameId); this.frameId = null;
    stopTracks(this.stream); this.stream = null;
    this.worker?.terminate(); this.worker = null;
    this.video.pause?.(); this.video.srcObject = null;
    this.active = false; this.ready = false; this.busy = false; this.lastVideoTime = -1; this.lastFrameAt = -Infinity;
    if (reject) reject(new Error('Camera setup cancelled.'));
    this.onStopped();
  }
}
