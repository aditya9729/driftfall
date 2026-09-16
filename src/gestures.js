// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// Camera-independent landmark interpretation, shared by live input and tests.
import { clamp, lerp, emptyInput } from './core.js';
const distance = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
export function describeHand(points, label = 'Unknown', score = 1) {
  if (!Array.isArray(points) || points.length !== 21 || score < .6) return null;
  if (points.some(p => !Number.isFinite(p?.x) || !Number.isFinite(p?.y))) return null;
  const span = distance(points[5], points[17]);
  if (span < .018) return null;
  // Mean fingertip-to-knuckle distance: ~1.0+ with fingers out, ~0.5 curled.
  const curl = ([[8, 5], [12, 9], [16, 13], [20, 17]]
    .reduce((n, [tip, knuckle]) => n + distance(points[tip], points[knuckle]), 0) / 4) / span;
  // Everything below uses mirrored screen coordinates, not anatomical labels.
  return { x: 1 - (points[0].x + points[5].x + points[9].x + points[17].x) / 4,
    y: (points[0].y + points[5].y + points[9].y + points[17].y) / 4,
    pinch: distance(points[4], points[8]) / span, curl, span, label, points };
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
export class Fist {
  constructor() { this.reset(); }
  reset() { this.active = false; this.since = null; }
  update(curl, now) {
    if (!Number.isFinite(curl)) { this.reset(); return false; }
    if (this.active) { if (curl > .78) this.reset(); }
    else if (curl < .62) {
      if (this.since === null) this.since = now;
      if (now - this.since >= 70) this.active = true;
    } else this.since = null;
    return this.active;
  }
}
export class Reach {
  constructor() { this.reset(); }
  reset() { this.active = false; this.since = null; }
  update(ratio, now) {
    if (!Number.isFinite(ratio)) { this.reset(); return false; }
    if (this.active) { if (ratio < 1.1) this.reset(); }
    else if (ratio > 1.22) {
      // A dwell, so leaning in while reaching for the keyboard is not a boost.
      if (this.since === null) this.since = now;
      if (now - this.since >= 90) this.active = true;
    } else this.since = null;
    return this.active;
  }
}
// One Euro filter. A single fixed lerp has to choose between jitter when the
// hand is still and lag when it moves; this widens the cutoff with speed, so it
// can be steady at rest and responsive in a fast correction.
// beta is SIGNAL-SCALE DEPENDENT: it multiplies the signal's own derivative, so
// a value tuned for pixels is far too small for a signal normalised to +/-1.
// Measured against the fixed filter this replaced (tau 60ms): these settings
// reach 90% of a step in .10s vs .167s, and hold a quarter of its tremor.
export class OneEuro {
  constructor({ minCutoff = 1.1, beta = 1.5, dCutoff = 1 } = {}) {
    this.minCutoff = minCutoff; this.beta = beta; this.dCutoff = dCutoff; this.reset();
  }
  reset() { this.value = null; this.prev = null; this.dx = 0; }
  static alpha(dt, cutoff) { const tau = 1 / (2 * Math.PI * cutoff); return 1 / (1 + tau / dt); }
  filter(value, dt) {
    if (!Number.isFinite(value)) return this.value ?? 0;
    if (this.value === null) { this.value = value; this.prev = value; return value; }
    const dx = (value - this.prev) / dt;
    this.dx = lerp(this.dx, dx, OneEuro.alpha(dt, this.dCutoff));
    const cutoff = this.minCutoff + this.beta * Math.abs(this.dx);
    this.value = lerp(this.value, value, OneEuro.alpha(dt, cutoff));
    this.prev = value;
    return this.value;
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
    // Aim gets a slightly wider cutoff: it travels further and must not lag.
    // Aim spans roughly +/-16 rather than +/-1, so its derivative is ~8x larger
    // and beta scales down to match.
    this.filters = { x: new OneEuro(), y: new OneEuro(),
      ax: new OneEuro({ beta: .2 }), ay: new OneEuro({ beta: .2 }) };
    this.firePinch = new Pinch(); this.fireFist = new Fist();
    this.boostPinch = new Pinch(); this.reach = new Reach();
    this.neutralSpan = null;
    this.lastSeen = -Infinity; this.lastUpdate = null; this.lastPilot = null;
    this.history = []; this.calibrated = false; this.last = emptyInput();
  }
  ingest(result, now) {
    const hands = (result?.landmarks || []).map((points, i) => {
      const category = (result.handedness || result.handednesses || [])[i]?.[0];
      return describeHand(points, category?.categoryName || `hand-${i}`, category?.score ?? 1);
    }).filter(Boolean);
    if (!hands.length) { this.last.fire = false; this.last.boost = false; this.firePinch.reset(); this.fireFist.reset(); this.boostPinch.reset(); this.reach.reset(); return false; }
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
    if (!pilot) { this.last.fire = false; this.last.boost = false; this.firePinch.reset(); this.fireFist.reset(); this.boostPinch.reset(); this.reach.reset(); return false; }
    const dt = this.lastUpdate === null ? .05 : clamp((now - this.lastUpdate) / 1000, .001, .15);
    this.lastUpdate = now; this.lastSeen = now; this.lastPilot = pilot;
    this.history.push({ pilot, gunner, now }); if (this.history.length > 16) this.history.shift();
    const dead = n => Math.abs(n) < .055 ? 0 : Math.sign(n) * (Math.abs(n) - .055) / .945;
    const tx = dead(clamp((pilot.x - this.neutral.x) * 4.3 * this.sensitivity, -1, 1));
    const ty = dead(clamp((this.neutral.y - pilot.y) * 5.2 * this.sensitivity, -1, 1));
    const ax = this.mode === 'one' ? tx * 8 : gunner ? clamp((gunner.x - this.aimNeutral.x) * 50 * this.sensitivity, -14, 14) : 0;
    const ay = this.mode === 'one' ? ty * 4 : gunner ? clamp((this.aimNeutral.y - gunner.y) * 38 * this.sensitivity, -9, 9) : 0;
    this.smooth.x = this.filters.x.filter(tx, dt); this.smooth.y = this.filters.y.filter(ty, dt);
    this.smooth.ax = this.filters.ax.filter(ax, dt); this.smooth.ay = this.filters.ay.filter(ay, dt);
    // One hand had no boost gesture at all - only the keyboard - so the default
    // control style could not use half the ship. Pushing the hand toward the
    // camera is the throttle; two-hand keeps the pilot pinch it documents.
    const reachRatio = this.neutralSpan ? pilot.span / this.neutralSpan : NaN;
    // Both detectors run every frame - never short-circuited - or the idle one
    // keeps stale dwell state and misfires the moment the other releases.
    const pinching = this.firePinch.update(gunner?.pinch, now);
    const fisted = this.fireFist.update(gunner?.curl, now);
    this.last = { x: this.smooth.x, y: this.smooth.y, target: true,
      fire: this.calibrated && (pinching || fisted),
      grip: fisted ? 'fist' : pinching ? 'pinch' : null,
      boost: this.calibrated && (this.mode === 'two'
        ? this.boostPinch.update(pilot.pinch, now)
        : this.reach.update(reachRatio, now)),
      reach: Number.isFinite(reachRatio) ? reachRatio : 1,
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
    // Neutral span is the hand's resting distance from the camera, the baseline
    // the throttle gesture is measured against.
    this.neutralSpan = mean('pilot', 'span');
    this.calibrated = true; this.smooth = { x: 0, y: 0, ax: 0, ay: 0 };
    for (const f of Object.values(this.filters)) f.reset();
    this.firePinch.reset(); this.fireFist.reset(); this.boostPinch.reset(); this.reach.reset(); this.last = emptyInput();
    return true;
  }
  input(now) {
    // Never reuse an old held pinch, even before the longer auto-pause threshold.
    if (!this.calibrated || now - this.lastSeen > 180) return { ...emptyInput(), target: true };
    return { ...this.last };
  }
  lost(now) { return now - this.lastSeen > 650; }
}
