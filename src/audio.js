// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
import { MUSIC, MusicSynth, scoreStep } from './music.js';
const KEY = 'driftfall.audio.v1';
const volume = (value, fallback) => typeof value === 'number' && Number.isFinite(value) ? Math.max(0, Math.min(1, value)) : fallback;
const safeStorage = () => { try { return globalThis.localStorage; } catch { return null; } };
export function loadAudioSettings(storage) {
  try { const s = JSON.parse(storage?.getItem(KEY) || '{}'); return { music: volume(s?.music, .48), effects: volume(s?.effects, .65) }; }
  catch { return { music: .48, effects: .65 }; }
}

export class AudioBus {
  constructor({ storage = safeStorage(), Context = null, clock = globalThis } = {}) {
    this.enabled = false; this.suspended = false; this.context = null; this.storage = storage;
    this.Context = Context; this.clock = clock; this.settings = loadAudioSettings(storage);
    this.scene = 'hangar'; this.intensity = 0; this.timer = null; this.revision = 0;
    this.nextStep = 0; this.nextTime = 0; this.scheduledSteps = 0; this.error = ''; this.fx = new Set();
  }
  ensureContext() {
    if (this.context && this.context.state !== 'closed') return;
    const Context = this.Context || globalThis.AudioContext || globalThis.webkitAudioContext;
    if (!Context) throw new Error('Web Audio is unavailable. The game remains playable without sound.');
    this.context = new Context({ latencyHint: 'interactive' });
    const ctx = this.context;
    this.master = ctx.createGain(); this.master.gain.value = 0;
    this.limiter = ctx.createDynamicsCompressor(); this.limiter.threshold.value = -12; this.limiter.knee.value = 10;
    this.limiter.ratio.value = 8; this.limiter.attack.value = .003; this.limiter.release.value = .2;
    this.music = ctx.createGain(); this.effects = ctx.createGain();
    this.music.gain.value = this.settings.music; this.effects.gain.value = this.settings.effects;
    this.music.connect(this.limiter); this.effects.connect(this.limiter); this.limiter.connect(this.master); this.master.connect(ctx.destination);
    this.synth = new MusicSynth(ctx, this.music);
  }
  async toggle() { return this.setEnabled(!this.enabled); }
  async setEnabled(enabled) {
    this.enabled = Boolean(enabled); this.error = '';
    await this.apply(); return this.enabled;
  }
  setScene(ui, intensity = 0) {
    this.scene = ui === 'playing' || ui === 'flight' ? 'flight' : ui === 'countdown' ? 'countdown' : ui === 'result' ? 'result' : 'hangar';
    this.intensity = volume(intensity, 0);
  }
  setVolume(channel, value) {
    if (!['music', 'effects'].includes(channel)) return;
    const wasSilent = this.settings.music === 0;
    this.settings[channel] = volume(value, this.settings[channel]);
    if (this.context && this[channel]) {
      const param = this[channel].gain, t = this.context.currentTime;
      param.cancelScheduledValues(t); param.setTargetAtTime(this.settings[channel], t, .025);
      if (channel === 'music' && wasSilent && this.settings.music > 0) { this.nextStep = 0; this.nextTime = t + .04; }
    }
    // Remember levels, never auto-enable audio on the next page visit.
    try { this.storage?.setItem(KEY, JSON.stringify(this.settings)); } catch {}
  }
  haltScheduling() { if (this.timer !== null) this.clock.clearInterval(this.timer); this.timer = null; }
  clearVoices() {
    this.synth?.stop();
    for (const item of this.fx) { try { item.osc.stop(); } catch {} item.osc.disconnect(); item.gain.disconnect(); }
    this.fx.clear();
  }
  async apply() {
    const revision = ++this.revision;
    this.haltScheduling();
    try {
      if (this.enabled && !this.suspended) {
        this.ensureContext();
        // Called synchronously from click/keyboard handlers, before the await:
        // no attempt to circumvent the browser's user-activation requirement.
        await this.context.resume();
        if (revision !== this.revision) return;
        if (this.context.state !== 'running') throw new Error('Audio is paused by the browser. Press SOUND again to retry.');
        this.clearVoices();
        const t = this.context.currentTime; this.master.gain.cancelScheduledValues(t); this.master.gain.setTargetAtTime(.82, t, .04);
        this.nextStep = 0; this.nextTime = t + .045;
        this.schedule(); this.timer = this.clock.setInterval(() => this.schedule(), 25);
      } else if (this.context && this.context.state !== 'closed') {
        const t = this.context.currentTime;
        this.master.gain.cancelScheduledValues(t); this.master.gain.setTargetAtTime(0, t, .006);
        // A brief fade, with a revision guard for rapid mute/unmute or tab changes.
        await new Promise(resolve => this.clock.setTimeout(resolve, 32));
        if (revision !== this.revision) return;
        this.clearVoices(); await this.context.suspend();
      }
    } catch (error) {
      if (revision !== this.revision) return;
      this.enabled = false; this.error = error?.message || 'Audio could not start.';
      this.haltScheduling(); this.clearVoices();
      try { await this.context?.suspend(); } catch {}
    }
  }
  schedule() {
    if (!this.enabled || this.suspended || this.context?.state !== 'running') return;
    const now = this.context.currentTime;
    // Never replay a burst of missed notes after a background/main-thread stall.
    if (this.nextTime < now - .15) { this.nextTime = now + .035; this.nextStep = Math.ceil(this.nextStep / 16) * 16; }
    let budget = 4;
    while (this.nextTime < now + .14 && budget-- > 0) {
      if (this.settings.music > 0) for (const event of scoreStep(this.nextStep, { scene: this.scene, intensity: this.intensity })) this.synth.play(event, this.nextTime);
      this.nextStep++; this.scheduledSteps++; this.nextTime += MUSIC.stepSeconds;
    }
  }
  tone(freq, duration, level = .025, type = 'sine', end = freq) {
    if (!this.enabled || this.suspended || this.settings.effects === 0 || this.context?.state !== 'running' || this.fx.size >= 32) return;
    if (![freq, duration, level, end].every(Number.isFinite) || duration <= 0) return;
    const t = this.context.currentTime, osc = this.context.createOscillator(), gain = this.context.createGain();
    const item = { osc, gain }; this.fx.add(item); duration = Math.min(duration, 2);
    osc.type = ['sine', 'triangle', 'square', 'sawtooth'].includes(type) ? type : 'sine';
    osc.frequency.setValueAtTime(Math.max(20, freq), t); osc.frequency.exponentialRampToValueAtTime(Math.max(20, end), t + duration);
    gain.gain.setValueAtTime(.0001, t); gain.gain.linearRampToValueAtTime(volume(level, .025), t + Math.min(.01, duration / 2));
    gain.gain.exponentialRampToValueAtTime(.0001, t + duration);
    osc.connect(gain); gain.connect(this.effects); osc.start(t); osc.stop(t + duration + .02);
    osc.onended = () => { osc.disconnect(); gain.disconnect(); this.fx.delete(item); };
  }
  events(events) {
    for (const e of events) {
      if (e.type === 'shot') this.tone(260, .07, .025, 'triangle', 100);
      if (e.type === 'gate') this.tone(520 + e.combo * 55, .16, .055, 'sine', 900);
      if (e.type === 'destroy') this.tone(95, .2, .05, 'sawtooth', 30);
      if (e.type === 'damage') this.tone(130, .3, .045, 'square', 45);
      if (e.type === 'cell') this.tone(660, .2, .045, 'sine', 1100);
      if (e.type === 'overheat') this.tone(180, .15, .04, 'triangle', 80);
    }
  }
  suspend() { this.suspended = true; return this.apply(); }
  resume() { this.suspended = false; return this.apply(); }
  snapshot() { return { enabled: this.enabled, suspended: this.suspended, context: this.context?.state || 'not-created', scene: this.scene, intensity: this.intensity, music: this.settings.music, effects: this.settings.effects, scheduledSteps: this.scheduledSteps, activeVoices: (this.synth?.voices.size || 0) + this.fx.size, schedulerActive: this.timer !== null, error: this.error, title: MUSIC.title }; }
  async dispose() { this.enabled = false; this.suspended = true; this.revision++; this.haltScheduling(); this.clearVoices(); this.synth?.dispose(); try { await this.context?.close(); } catch {} }
}
