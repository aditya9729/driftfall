// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// Pure game rules. No DOM, camera, rendering, storage, network or wall clock.
export const VERSION = '0.1.0';
export const RULESET = 3;
export const STEP = 1 / 120;
export const COURSE_LENGTH = 2100;
export const MODES = Object.freeze(['race', 'swarm', 'daily']);
// The Lattice: the derelict machine swarm that keeps the rift open.
export const ENEMY_NAMES = Object.freeze({ drone: 'SEEKER', block: 'PYLON', cell: 'CELL' });
// Hostiles are announced in groups of this many gate clusters.
export const WAVE_GROUP = 4;
// Hostile spread around the gate line.
export const LANE_X = 7.4, LANE_Y = 4.2;
// Pilots. VESPER's multipliers are all exactly 1, so the default pilot flies
// identically to previous builds; the others trade real advantages off.
export const CHARACTERS = Object.freeze({
  vesper:  { id:'vesper',  name:'VESPER',  role:'BALANCED',  blurb:'Steady hands. No weakness, no edge.',
             speed:1,    agility:1,    hull:1,    cool:1,    fire:1,   tint:[.55,.95,1] },
  kite:    { id:'kite',    name:'KITE',    role:'INTERCEPT', blurb:'Faster and sharper. Thin hull.',
             speed:1.08, agility:1.28, hull:.8,   cool:1,    fire:1,   tint:[.75,1,.55] },
  bastion: { id:'bastion', name:'BASTION', role:'ASSAULT',   blurb:'Heavy plating, cool barrel. Turns slow.',
             speed:.95,  agility:.86,  hull:1.25, cool:.85,  fire:1,   tint:[1,.72,.42] },
  ember:   { id:'ember',   name:'EMBER',   role:'GUNNER',    blurb:'Rapid fire, hotter barrel, light frame.',
             speed:1,    agility:1.05, hull:.9,   cool:1.08, fire:.78, tint:[1,.5,.62] }
});
export const DEFAULT_CHARACTER = 'vesper';
export function character(id) { return CHARACTERS[id] || CHARACTERS[DEFAULT_CHARACTER]; }
// Which hostiles a course fields, how many, and how wide they sit.
export const MIXES = Object.freeze(['balanced', 'seekers', 'pylons']);
export const DEFAULT_FLIGHT = Object.freeze({ mix: 'balanced', density: 1, spread: 1 });
export function cleanFlight(flight = {}) {
  return {
    mix: MIXES.includes(flight?.mix) ? flight.mix : 'balanced',
    density: clamp(Math.round(finite(Number(flight?.density), 1) * 10) / 10, .5, 2),
    spread: clamp(Math.round(finite(Number(flight?.spread), 1) * 10) / 10, .5, 1.6)
  };
}
export function isDefaultFlight(flight) {
  const f = cleanFlight(flight);
  return f.mix === 'balanced' && f.density === 1 && f.spread === 1;
}
// A short stable tag so a customised course keys its own ghosts and shares cleanly.
export function flightTag(flight, characterId) {
  const f = cleanFlight(flight), c = character(characterId).id;
  return isDefaultFlight(f) && c === DEFAULT_CHARACTER
    ? 'STD' : hash(`${f.mix}:${f.density}:${f.spread}:${c}`).toString(36).toUpperCase().slice(0, 5);
}
export const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
export const lerp = (a, b, t) => a + (b - a) * t;
export const finite = (n, fallback = 0) => Number.isFinite(n) ? n : fallback;
export function hash(text) {
  let h = 2166136261;
  for (const c of String(text)) h = Math.imul(h ^ c.charCodeAt(0), 16777619);
  return h >>> 0;
}
export function random(seed) {
  let a = typeof seed === 'number' ? seed >>> 0 : hash(seed);
  return () => {
    a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ a >>> 15, 1 | a);
    t ^= t + Math.imul(t ^ t >>> 7, 61 | t);
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}
export function cleanSeed(value) {
  return String(value || 'NEBULA-01').toUpperCase().replace(/[^A-Z0-9-]/g, '').slice(0, 32) || 'NEBULA-01';
}
export function dailySeed(date = new Date()) {
  return `DAILY-${date.toISOString().slice(0, 10)}`;
}
export function emptyInput() {
  return { x: 0, y: 0, target: false, fire: false, boost: false, aimX: 0, aimY: 0, aimActive: false };
}
export function course(seed, mode = 'race', flight = DEFAULT_FLIGHT) {
  const f = cleanFlight(flight);
  const rng = random(cleanSeed(seed));
  const length = mode === 'swarm' ? 4800 : COURSE_LENGTH;
  const gates = [], entities = [];
  let id = 0;
  for (let z = 90, i = 0; z < length - 40; z += 46, i++) {
    const x = Math.sin(i * 0.55) * 4.2 + (rng() - .5) * .8;
    const y = Math.sin(i * 0.37) * 2.1;
    gates.push({ id: id++, x, y, z, radius: 2.65, passed: false, collected: false, perfect: false });
    // Early flight teaches gates before introducing hazards.
    if (i < 2) continue;
    const wave = Math.floor((i - 2) / WAVE_GROUP);
    const base = mode === 'swarm' ? 4 : (i % 3 === 0 ? 3 : 2);
    const count = clamp(Math.round(base * f.density), 1, 9);
    for (let j = 0; j < count; j++) {
      // A single-type mix draws no rng for the type, which is a different course
      // than 'balanced' - and that is exactly what picking a mix is asking for.
      const type = f.mix === 'seekers' ? 'drone' : f.mix === 'pylons' ? 'block'
        : (j === 0 || rng() < .4) ? 'drone' : 'block';
      // Hostiles sit near the gate line, not scattered to the periphery: a lane
      // you must actually shoot or dodge through. Same rng draws, tighter spread.
      const ex = clamp(x + (rng() - .5) * LANE_X * f.spread, -8.1, 8.1);
      const ey = clamp(y + (rng() - .5) * LANE_Y * f.spread, -3.8, 3.8);
      entities.push({ id: id++, type, wave, x: ex, y: ey, z: z + 17 + j * 5,
        size: type === 'drone' ? .8 : 1.15, hp: type === 'drone' ? 2 : 3,
        dead: false, collided: false });
    }
    if (i % 4 === 0) entities.push({ id: id++, type: 'cell', wave, x: -x * .65, y: -y * .8,
      z: z + 28, size: .65, hp: 1, dead: false, collided: false });
  }
  // Waves are grouped from the layout that already exists. This consumes no extra
  // rng draws, so every seeded course - and every saved ghost - is unchanged.
  const grouped = [];
  for (const e of entities) {
    if (e.type === 'cell') continue;
    const w = grouped[e.wave] || (grouped[e.wave] = { index: e.wave, z: e.z, count: 0, seekers: 0, pylons: 0 });
    w.z = Math.min(w.z, e.z);
    w.count++;
    if (e.type === 'drone') w.seekers++; else w.pylons++;
  }
  // Announce a wave roughly one gate-spacing before its first hostile.
  const waves = grouped.filter(Boolean).map(w => ({ ...w, z: Math.max(0, w.z - 46) }));
  const hostileTotal = waves.reduce((n, w) => n + w.count, 0);
  // Reachable in a decent run, so the reward actually lands; capped for swarm's
  // much denser course.
  const objectiveTarget = Math.min(40, Math.max(3, Math.round(hostileTotal * .3)));
  return { length, gates, entities, waves, hostileTotal, objectiveTarget, flight: f };
}
// Slab-based segment/AABB intersection catches fast shots and thin blocks.
export function segmentBox(a, b, center, half) {
  let tmin = 0, tmax = 1;
  for (const axis of ['x', 'y', 'z']) {
    const d = b[axis] - a[axis];
    const lo = center[axis] - half, hi = center[axis] + half;
    if (Math.abs(d) < 1e-9) {
      if (a[axis] < lo || a[axis] > hi) return null;
    } else {
      let ta = (lo - a[axis]) / d, tb = (hi - a[axis]) / d;
      if (ta > tb) [ta, tb] = [tb, ta];
      tmin = Math.max(tmin, ta); tmax = Math.min(tmax, tb);
      if (tmin > tmax) return null;
    }
  }
  return tmin;
}
export class Run {
  constructor({ seed = 'NEBULA-01', mode = 'race', flight = DEFAULT_FLIGHT, character: pilot = DEFAULT_CHARACTER } = {}) {
    this.seed = cleanSeed(seed); this.mode = MODES.includes(mode) ? mode : 'race';
    this.pilot = character(pilot); this.character = this.pilot.id;
    Object.assign(this, course(this.seed, this.mode, flight));
    this.tag = flightTag(this.flight, this.character);
    this.player = { x: 0, y: 0, vx: 0, vy: 0 };
    this.distance = 0; this.elapsed = 0; this.speed = 0;
    // Hull scales with the pilot; the HUD reads a percentage of maxHealth.
    this.maxHealth = Math.round(100 * this.pilot.hull);
    this.health = this.maxHealth; this.energy = 100; this.heat = 0; this.overheated = false;
    this.score = 0; this.combo = 0; this.bestCombo = 0; this.gateCount = 0;
    this.kills = 0; this.shots = 0; this.hitCount = 0; this.perfects = 0;
    this.bullets = []; this.events = []; this.samples = [];
    this.cooldown = 0; this.invulnerable = 0; this.sampleAt = 0;
    this.status = 'running'; this.boosting = false; this.tick = 0;
    this.waveIndex = -1; this.objectiveDone = false;
  }
  emit(type, data = {}) { this.events.push({ type, t: this.elapsed, ...data }); }
  damage(amount, entity) {
    if (this.invulnerable > 0) return;
    this.health = Math.max(0, this.health - amount); this.invulnerable = 1.1;
    this.combo = 0;
    this.emit('damage', { x: this.player.x, y: this.player.y, z: this.distance, id: entity?.id,
      amount, kind: entity?.type || null });
  }
  step(raw = emptyInput(), dt = STEP) {
    if (this.status !== 'running') return;
    // A bounded step is a contract, not a way to skip simulation time.
    if (!Number.isFinite(dt) || dt <= 0 || dt > .05) throw new RangeError('step must be in (0, 0.05]');
    this.tick++; this.elapsed += dt;
    const input = { ...emptyInput(), ...raw };
    input.x = clamp(finite(input.x), -1, 1); input.y = clamp(finite(input.y), -1, 1);
    const p = this.player, old = { x: p.x, y: p.y, z: this.distance };
    const ag = this.pilot.agility;
    const vx = input.target ? clamp((input.x * 8 - p.x) * 5 * ag, -13 * ag, 13 * ag) : input.x * 11 * ag;
    const vy = input.target ? clamp((input.y * 4 - p.y) * 5 * ag, -9 * ag, 9 * ag) : input.y * 8 * ag;
    p.vx = lerp(p.vx, vx, 1 - Math.exp(-12 * dt));
    p.vy = lerp(p.vy, vy, 1 - Math.exp(-12 * dt));
    p.x = clamp(p.x + p.vx * dt, -8, 8); p.y = clamp(p.y + p.vy * dt, -4, 4);
    // A tiny residual energy is insufficient to stutter-boost every frame.
    this.boosting = Boolean(input.boost && this.energy >= (this.boosting ? .5 : 12));
    this.energy = clamp(this.energy + (this.boosting ? -29 : 13) * dt, 0, 100);
    this.speed = lerp(this.speed, (this.boosting ? 48 : 28) * this.pilot.speed, 1 - Math.exp(-2.7 * dt));
    this.distance += this.speed * dt;
    // Announce each hostile wave exactly once, before it is in weapons range.
    // A loop, not an if: a boosting run can cross more than one marker per step.
    while (this.waveIndex + 1 < this.waves.length && this.distance >= this.waves[this.waveIndex + 1].z) {
      const w = this.waves[++this.waveIndex];
      this.emit('wave', { index: w.index, total: this.waves.length,
        count: w.count, seekers: w.seekers, pylons: w.pylons });
    }
    this.invulnerable = Math.max(0, this.invulnerable - dt);
    this.cooldown = Math.max(0, this.cooldown - dt);
    this.heat = Math.max(0, this.heat - .3 * dt);
    if (this.overheated && this.heat < .28) this.overheated = false;
    if (input.fire && this.cooldown <= 0 && !this.overheated) {
      let ax = input.aimActive ? clamp(finite(input.aimX), -16, 16) : p.x;
      let ay = input.aimActive ? clamp(finite(input.aimY), -10, 10) : p.y;
      let slopeX = (ax - p.x) / 55, slopeY = (ay - p.y) / 55;
      // A narrow assist cone; it never snaps to distant off-reticle targets.
      let best = .085, assisted = null;
      for (const e of this.entities) {
        const dz = e.z - this.distance;
        if (e.dead || e.type === 'cell' || dz < 5 || dz > 95) continue;
        const error = Math.hypot((e.x - p.x) / dz - slopeX, (e.y - p.y) / dz - slopeY);
        if (error < best) { best = error; assisted = e; }
      }
      if (assisted) {
        slopeX = (assisted.x - p.x) / (assisted.z - this.distance);
        slopeY = (assisted.y - p.y) / (assisted.z - this.distance);
      }
      this.bullets.push({ x: p.x, y: p.y, z: this.distance + 1, sx: slopeX, sy: slopeY, life: 1.35 });
      this.shots++; this.cooldown = .13 * this.pilot.fire;
      this.heat = Math.min(1, this.heat + .087 * this.pilot.cool);
      if (this.heat >= .999) { this.overheated = true; this.emit('overheat'); }
      this.emit('shot');
    }
    for (const bullet of this.bullets) {
      const a = { x: bullet.x, y: bullet.y, z: bullet.z };
      bullet.z += 185 * dt; bullet.x += bullet.sx * 185 * dt; bullet.y += bullet.sy * 185 * dt; bullet.life -= dt;
      let hit = null, first = Infinity;
      for (const e of this.entities) {
        if (e.dead || e.type === 'cell' || e.z < a.z - 2 || e.z > bullet.z + 2) continue;
        const t = segmentBox(a, bullet, e, e.size + .14);
        if (t !== null && t < first) { first = t; hit = e; }
      }
      if (hit) {
        bullet.life = -1; hit.hp--; this.hitCount++;
        this.emit('hit', { x: hit.x, y: hit.y, z: hit.z });
        if (hit.hp <= 0) {
          hit.dead = true; this.kills++; this.score += hit.type === 'drone' ? 250 : 100;
          this.energy = Math.min(100, this.energy + 4);
          this.emit('destroy', { x: hit.x, y: hit.y, z: hit.z, kind: hit.type });
          if (!this.objectiveDone && this.kills >= this.objectiveTarget) {
            this.objectiveDone = true; this.score += 1000;
            this.emit('objective', { target: this.objectiveTarget });
          }
        }
      }
    }
    this.bullets = this.bullets.filter(b => b.life > 0 && b.z >= this.distance - 10);
    const now = { x: p.x, y: p.y, z: this.distance };
    for (const e of this.entities) {
      if (e.dead || e.collided || e.z < old.z - 3 || e.z > now.z + 3) continue;
      if (segmentBox(old, now, e, e.size + .48) !== null) {
        e.collided = true; e.dead = true;
        if (e.type === 'cell') {
          this.health = Math.min(this.maxHealth, this.health + 15); this.energy = Math.min(100, this.energy + 25);
          this.score += 75; this.emit('cell', { x: e.x, y: e.y, z: e.z });
        } else this.damage(20, e);
      }
    }
    for (const g of this.gates) {
      if (g.passed || g.z > this.distance) continue;
      g.passed = true;
      const t = clamp((g.z - old.z) / Math.max(.0001, now.z - old.z), 0, 1);
      const miss = Math.hypot(lerp(old.x, p.x, t) - g.x, lerp(old.y, p.y, t) - g.y);
      if (miss < g.radius - .4) {
        g.collected = true; g.perfect = miss < .75;
        this.gateCount++; this.combo = Math.min(8, this.combo + 1);
        this.bestCombo = Math.max(this.combo, this.bestCombo);
        this.score += 150 * this.combo + (g.perfect ? 150 : 0);
        if (g.perfect) this.perfects++;
        this.energy = Math.min(100, this.energy + 7);
        this.emit('gate', { perfect: g.perfect, combo: this.combo, x: g.x, y: g.y, z: g.z });
      } else { this.combo = 0; this.emit('miss'); }
    }
    if (this.elapsed >= this.sampleAt) {
      if (this.samples.length < 2400) this.samples.push([
        +this.elapsed.toFixed(3), +this.distance.toFixed(3), +p.x.toFixed(3), +p.y.toFixed(3)
      ]);
      this.sampleAt += .1;
    }
    if (this.health <= 0) { this.status = 'lost'; this.emit('end'); }
    else if ((this.mode === 'swarm' && this.elapsed >= 90) || (this.mode !== 'swarm' && this.distance >= this.length)) {
      this.status = 'won'; this.score += Math.round(this.health * 20); this.emit('end');
    }
    // Events are visual/audio notifications; consumers can drain them once a frame.
    if (this.events.length > 128) this.events.splice(0, this.events.length - 128);
  }
  drainEvents() { return this.events.splice(0); }
  snapshot() {
    const next = this.gates.find(g => !g.passed);
    return { seed: this.seed, mode: this.mode, status: this.status, elapsed: this.elapsed,
      distance: this.distance, length: this.length, player: { ...this.player }, speed: this.speed,
      health: this.health, energy: this.energy, heat: this.heat, score: this.score,
      gates: this.gateCount, kills: this.kills, shots: this.shots, combo: this.combo,
      wave: { current: this.waveIndex + 1, total: this.waves.length },
      objective: { target: this.objectiveTarget,
        progress: Math.min(this.kills, this.objectiveTarget), done: this.objectiveDone },
      nextGate: next ? { x: next.x, y: next.y, z: next.z } : null };
  }
}
