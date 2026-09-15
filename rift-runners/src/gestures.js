// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// Camera-independent landmark interpretation, shared by live input and tests.
import { clamp, lerp, emptyInput } from './core.js';
const distance = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
export function describeHand(points, label = 'Unknown', score = 1) {
  if (!Array.isArray(points) || points.length !== 21 || score < .6) return null;
  if (points.some(p => !Number.isFinite(p?.x) || !Number.isFinite(p?.y))) return null;
  const span = distance(points[5], points[17]);
  if (span < .018) return null;
  // Everything below uses mirrored screen coordinates, not anatomical labels.
  return { x: 1 - (points[0].x + points[5].x + points[9].x + points[17].x) / 4,
    y: (points[0].y + points[5].y + points[9].y + points[17].y) / 4,
    pinch: distance(points[4], points[8]) / span, label, points };
}
export class Pinch {
  constructor() { this.reset(); }
  reset() { this.active = false; this.since = null; }
  update(ratio, now) {
    if (!Number.isFinite(ratio)) { this.reset(); return false; }
    if (this.active) { if (ratio > .5) this.reset(); }
    else if (ratio < .32) {
      if (this.since === null) this.since = now;
      if (now - this.since >= 65) this.active = true;
    } else this.since = null;
    return this.active;
  }
}
export class HandMapper {
  constructor({ mode = 'one', swap = false, sensitivity = 1 } = {}) {
    this.mode = mode; this.swap = swap; this.sensitivity = sensitivity;
    this.reset();
  }
  reset() {
    this.pilotLabel = null; this.gunnerLabel = null;
    this.neutral = { x: .5, y: .55 }; this.aimNeutral = { x: .72, y: .55 };
    this.smooth = { x: 0, y: 0, ax: 0, ay: 0 };
    this.firePinch = new Pinch(); this.boostPinch = new Pinch();
    this.lastSeen = -Infinity; this.lastUpdate = null; this.lastPilot = null;
    this.history = []; this.calibrated = false; this.last = emptyInput();
  }
  ingest(result, now) {
    const hands = (result?.landmarks || []).map((points, i) => {
      const category = (result.handedness || result.handednesses || [])[i]?.[0];
      return describeHand(points, category?.categoryName || `hand-${i}`, category?.score ?? 1);
    }).filter(Boolean);
    if (!hands.length) { this.last.fire = false; this.last.boost = false; this.firePinch.reset(); this.boostPinch.reset(); return false; }
    // Lock roles during calibration and retain labels when the detector reorders.
    if (this.pilotLabel === null) {
      const sorted = [...hands].sort((a, b) => a.x - b.x);
      if (this.mode === 'two' && sorted.length < 2) return false;
      const pilot = this.mode === 'one' ? sorted[0] : sorted[this.swap ? 1 : 0];
      this.pilotLabel = pilot.label;
      this.gunnerLabel = this.mode === 'two' ? sorted[this.swap ? 0 : 1].label : pilot.label;
    }
    const pilot = hands.find(h => h.label === this.pilotLabel);
    const gunner = hands.find(h => h.label === this.gunnerLabel);
    if (!pilot) { this.last.fire = false; this.last.boost = false; this.firePinch.reset(); this.boostPinch.reset(); return false; }
    const dt = this.lastUpdate === null ? .05 : clamp((now - this.lastUpdate) / 1000, .001, .15);
    this.lastUpdate = now; this.lastSeen = now; this.lastPilot = pilot;
    this.history.push({ pilot, gunner, now }); if (this.history.length > 16) this.history.shift();
    const dead = n => Math.abs(n) < .055 ? 0 : Math.sign(n) * (Math.abs(n) - .055) / .945;
    const tx = dead(clamp((pilot.x - this.neutral.x) * 4.3 * this.sensitivity, -1, 1));
    const ty = dead(clamp((this.neutral.y - pilot.y) * 5.2 * this.sensitivity, -1, 1));
    const ax = this.mode === 'one' ? tx * 8 : gunner ? clamp((gunner.x - this.aimNeutral.x) * 50 * this.sensitivity, -14, 14) : 0;
    const ay = this.mode === 'one' ? ty * 4 : gunner ? clamp((this.aimNeutral.y - gunner.y) * 38 * this.sensitivity, -9, 9) : 0;
    const alpha = 1 - Math.exp(-dt / .06);
    this.smooth.x = lerp(this.smooth.x, tx, alpha); this.smooth.y = lerp(this.smooth.y, ty, alpha);
    this.smooth.ax = lerp(this.smooth.ax, ax, alpha); this.smooth.ay = lerp(this.smooth.ay, ay, alpha);
    this.last = { x: this.smooth.x, y: this.smooth.y, target: true,
      fire: this.calibrated && this.firePinch.update(gunner?.pinch, now),
      boost: this.calibrated && this.mode === 'two' && this.boostPinch.update(pilot.pinch, now),
      aimX: this.smooth.ax, aimY: this.smooth.ay, aimActive: true };
    return true;
  }
  canCalibrate(now) {
    const h = this.history.filter(h => now - h.now < 850);
    if (h.length < 8 || now - this.lastSeen > 180) return false;
    if (this.mode === 'two' && h.some(h => !h.gunner)) return false;
    const range = axis => Math.max(...h.map(h => h.pilot[axis])) - Math.min(...h.map(h => h.pilot[axis]));
    return range('x') < .10 && range('y') < .10;
  }
  calibrate(now) {
    if (!this.canCalibrate(now)) return false;
    const samples = this.history.filter(h => now - h.now < 850);
    const mean = (role, axis) => samples.reduce((n, h) => n + h[role][axis], 0) / samples.length;
    this.neutral = { x: mean('pilot', 'x'), y: mean('pilot', 'y') };
    this.aimNeutral = { x: mean(this.mode === 'one' ? 'pilot' : 'gunner', 'x'), y: mean(this.mode === 'one' ? 'pilot' : 'gunner', 'y') };
    this.calibrated = true; this.smooth = { x: 0, y: 0, ax: 0, ay: 0 };
    this.firePinch.reset(); this.boostPinch.reset(); this.last = emptyInput();
    return true;
  }
  input(now) {
    // Never reuse an old held pinch, even before the longer auto-pause threshold.
    if (!this.calibrated || now - this.lastSeen > 180) return { ...emptyInput(), target: true };
    return { ...this.last };
  }
  lost(now) { return now - this.lastSeen > 650; }
}
