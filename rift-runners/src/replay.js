// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
import { RULESET, MODES, COURSE_LENGTH, clamp, lerp, cleanSeed, cleanFlight, character, flightTag } from './core.js';
export const MAX_REPLAY_BYTES = 180_000;
export function record(run) {
  return { format: 'driftfall-ghost', version: RULESET, seed: run.seed, mode: run.mode,
    status: run.status, score: run.score, elapsed: run.elapsed,
    // A ghost is only comparable against the same course build and the same pilot.
    flight: cleanFlight(run.flight), character: character(run.character).id,
    samples: run.samples.map(s => s.map(n => Object.is(n, -0) ? 0 : n)) };
}
export function validateReplay(value, { local = false } = {}) {
  if (!value || value.format !== 'driftfall-ghost' || value.version !== RULESET) throw new Error('This is not a compatible DRIFTFALL ghost.');
  if (typeof value.seed !== 'string' || !/^[A-Z0-9-]{1,32}$/.test(value.seed)) throw new Error('Invalid course seed.');
  if (!MODES.includes(value.mode)) throw new Error('Unknown flight mode.');
  // An imported file must be a genuine finished run. A locally recorded attempt
  // is allowed to be a loss, so there is always a pace line to race.
  if (!local && value.status !== 'won') throw new Error('Only completed flights can be imported.');
  if (!['won', 'lost'].includes(value.status)) throw new Error('Unknown flight status.');
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
  if (value.status === 'won') {
    if (value.mode !== 'swarm' && prevD < COURSE_LENGTH - 8) throw new Error('Ghost did not finish the route.');
    if (value.mode === 'swarm' && value.elapsed < 89.99) throw new Error('Ghost did not finish survival.');
  }
  // Reconstruct a whitelist. Never preserve arbitrary properties from a file.
  return { format: 'driftfall-ghost', version: RULESET, seed: value.seed, mode: value.mode,
    status: value.status, score: Math.round(value.score), elapsed: value.elapsed,
    flight: cleanFlight(value.flight), character: character(value.character).id, samples };
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
  // A finished run always beats an unfinished one; among unfinished attempts the
  // one that got further is the better pace line.
  if (next.status !== old.status) return next.status === 'won';
  if (next.status === 'lost') return lastDistance(next) > lastDistance(old);
  return next.mode === 'swarm' ? next.score > old.score : next.elapsed < old.elapsed;
}
export function lastDistance(ghost) {
  const ss = ghost?.samples;
  return ss?.length ? ss[ss.length - 1][1] : 0;
}
export class GhostStore {
  constructor(storage) { this.storage = storage; this.available = true; }
  // The tag keys a ghost to its course build and pilot, so a custom flight never
  // races a ghost flown on a different course.
  key(seed, mode, flight, characterId) {
    return `driftfall.ghost.v${RULESET}.${mode}.${flightTag(flight, characterId)}.${cleanSeed(seed)}`;
  }
  load(seed, mode, flight, characterId) {
    try {
      const text = this.storage?.getItem(this.key(seed, mode, flight, characterId));
      if (!text) return null;
      const ghost = validateReplay(JSON.parse(text), { local: true });
      return ghost.seed === seed && ghost.mode === mode ? ghost : null;
    } catch { this.available = false; return null; }
  }
  save(ghost) {
    try {
      const safe = validateReplay(ghost, { local: true });
      const key = this.key(safe.seed, safe.mode, safe.flight, safe.character);
      const old = this.load(safe.seed, safe.mode, safe.flight, safe.character);
      if (!isBetter(safe, old)) return false;
      if (!this.storage) throw new Error('Storage unavailable');
      this.storage.setItem(key, JSON.stringify(safe));
      // Keep at most 12 course ghosts; no unbounded daily accumulation.
      const keys = Object.keys(this.storage).filter(k => k.startsWith(`driftfall.ghost.v${RULESET}.`));
      for (const k of keys.filter(k => k !== key).slice(0, Math.max(0, keys.length - 12))) {
        if (k !== key) this.storage.removeItem(k);
      }
      return true;
    } catch { this.available = false; return false; }
  }
  clear() {
    try { for (const k of Object.keys(this.storage || {})) if (k.startsWith('driftfall.ghost.')) this.storage.removeItem(k); }
    catch { this.available = false; }
  }
}
