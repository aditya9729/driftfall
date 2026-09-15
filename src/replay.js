// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
import { RULESET, MODES, COURSE_LENGTH, clamp, lerp, cleanSeed } from './core.js';
export const MAX_REPLAY_BYTES = 180_000;
export function record(run) {
  return { format: 'driftfall-ghost', version: RULESET, seed: run.seed, mode: run.mode,
    status: run.status, score: run.score, elapsed: run.elapsed,
    samples: run.samples.map(s => s.map(n => Object.is(n, -0) ? 0 : n)) };
}
export function validateReplay(value) {
  if (!value || value.format !== 'driftfall-ghost' || value.version !== RULESET) throw new Error('This is not a compatible DRIFTFALL ghost.');
  if (typeof value.seed !== 'string' || !/^[A-Z0-9-]{1,32}$/.test(value.seed)) throw new Error('Invalid course seed.');
  if (!MODES.includes(value.mode) || value.status !== 'won') throw new Error('Only completed flights can be imported.');
  if (!Number.isFinite(value.elapsed) || value.elapsed <= 0 || value.elapsed > 200) throw new Error('Invalid flight duration.');
  if (!Number.isFinite(value.score) || value.score < 0 || value.score > 1e8) throw new Error('Invalid score.');
  if (!Array.isArray(value.samples) || value.samples.length < 2 || value.samples.length > 2400) throw new Error('Invalid ghost length.');
  let prevT = -1, prevD = -1;
  const samples = value.samples.map(s => {
    if (!Array.isArray(s) || s.length !== 4 || s.some(n => !Number.isFinite(n))) throw new Error('Invalid ghost coordinates.');
    const [t, d, x, y] = s;
    if (t < 0 || t <= prevT || t > 201 || t > value.elapsed + .15 || d < prevD || d < 0 || d > 10_000 || Math.abs(x) > 8.01 || Math.abs(y) > 4.01) throw new Error('Ghost data is out of bounds.');
    if (prevT >= 0 && (d - prevD) / (t - prevT) > 65) throw new Error('Ghost speed is out of bounds.');
    prevT = t; prevD = d; return [t, d, x, y];
  });
  if (samples[0][0] > .2 || samples[0][1] > 1 || value.elapsed - prevT > .25) throw new Error('Ghost is incomplete.');
  if (value.mode !== 'swarm' && prevD < COURSE_LENGTH - 8) throw new Error('Ghost did not finish the route.');
  if (value.mode === 'swarm' && value.elapsed < 89.99) throw new Error('Ghost did not finish survival.');
  // Reconstruct a whitelist. Never preserve arbitrary properties from a file.
  return { format: 'driftfall-ghost', version: RULESET, seed: value.seed, mode: value.mode,
    status: 'won', score: Math.round(value.score), elapsed: value.elapsed, samples };
}
export function parseReplay(text) {
  if (typeof text !== 'string' || new TextEncoder().encode(text).length > MAX_REPLAY_BYTES) throw new Error('Ghost file is too large.');
  return validateReplay(JSON.parse(text));
}
export function ghostAt(ghost, t) {
  if (!ghost?.samples?.length || t < 0 || t > ghost.elapsed) return null;
  const ss = ghost.samples;
  let lo = 0, hi = ss.length - 1;
  while (lo + 1 < hi) { const mid = (lo + hi) >> 1; if (ss[mid][0] <= t) lo = mid; else hi = mid; }
  const a = ss[lo], b = ss[hi]; const f = clamp((t - a[0]) / Math.max(.001, b[0] - a[0]), 0, 1);
  return { distance: lerp(a[1], b[1], f), x: lerp(a[2], b[2], f), y: lerp(a[3], b[3], f) };
}
export function isBetter(next, old) {
  if (!old) return true;
  return next.mode === 'swarm' ? next.score > old.score : next.elapsed < old.elapsed;
}
export class GhostStore {
  constructor(storage) { this.storage = storage; this.available = true; }
  key(seed, mode) { return `driftfall.ghost.v${RULESET}.${mode}.${cleanSeed(seed)}`; }
  load(seed, mode) {
    try {
      const text = this.storage?.getItem(this.key(seed, mode));
      if (!text) return null;
      const ghost = parseReplay(text);
      return ghost.seed === seed && ghost.mode === mode ? ghost : null;
    } catch { this.available = false; return null; }
  }
  save(ghost) {
    try {
      const safe = validateReplay(ghost);
      const old = this.load(safe.seed, safe.mode);
      if (!isBetter(safe, old)) return false;
      if (!this.storage) throw new Error('Storage unavailable');
      this.storage.setItem(this.key(safe.seed, safe.mode), JSON.stringify(safe));
      // Keep at most 12 course ghosts; no unbounded daily accumulation.
      const keys = Object.keys(this.storage).filter(k => k.startsWith(`driftfall.ghost.v${RULESET}.`));
      for (const k of keys.filter(k => k !== this.key(safe.seed, safe.mode)).slice(0, Math.max(0, keys.length - 12))) {
        if (k !== this.key(safe.seed, safe.mode)) this.storage.removeItem(k);
      }
      return true;
    } catch { this.available = false; return false; }
  }
  clear() {
    try { for (const k of Object.keys(this.storage || {})) if (k.startsWith('driftfall.ghost.')) this.storage.removeItem(k); }
    catch { this.available = false; }
  }
}
