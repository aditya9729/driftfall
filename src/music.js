// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// NEON WAKE: an original, deterministic synthwave score. No recorded samples,
// network requests, microphone, vendor SDK or third-party song is involved.
export const MUSIC = Object.freeze({ title: 'NEON WAKE', bpm: 116, stepsPerBar: 16, stepSeconds: 60 / 116 / 4 });
const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, Number.isFinite(n) ? n : lo));
const CHORDS = [
  { bass: 38, notes: [62, 65, 69, 76] }, // Dm(add9)
  { bass: 34, notes: [62, 65, 69, 72] }, // Bbmaj9
  { bass: 41, notes: [60, 64, 69, 67] }, // Fmaj9
  { bass: 36, notes: [60, 64, 67, 74] }, // C(add9)
];
const ARP = [0, 2, 1, 3, 2, 1, 3, 2];
const MELODIES = [[2, -1, 3, 2, 1, -1, 0, 1], [3, -1, 2, 1, 2, -1, 3, 2], [0, 1, -1, 2, 3, -1, 2, 1], [2, -1, 1, 0, 1, 2, -1, 0]];
export const midiHz = note => 440 * 2 ** ((note - 69) / 12);

// Pure score: useful for deterministic tests and rendering exactly the in-game
// arrangement with OfflineAudioContext. One event list per sixteenth note.
export function scoreStep(index, { scene = 'hangar', intensity = 0 } = {}) {
  const step = Math.max(0, Math.floor(Number.isFinite(index) ? index : 0));
  const beat = step % 16, bar = Math.floor(step / 16), phrase = Math.floor(bar / 8) % 4;
  const chord = CHORDS[Math.floor(bar / 2) % CHORDS.length];
  const flight = scene === 'flight', energy = clamp(intensity, 0, 1);
  const notes = [], tick = MUSIC.stepSeconds;
  const add = (instrument, note, duration, velocity, pan = 0) => notes.push({ instrument, note, duration, velocity, pan });
  if (beat === 0 && bar % 2 === 0) {
    chord.notes.forEach((n, i) => add('pad', n, tick * 31, flight ? .045 : .065, (i - 1.5) * .4));
  }
  if (flight) {
    if ([0, 3, 6, 8, 11, 14].includes(beat)) add('bass', chord.bass + (beat === 11 ? 12 : 0), tick * 1.65, .22);
    if (beat % 4 === 0) add('kick', 0, .28, .62);
    if (beat === 4 || beat === 12) add('snare', 0, .17, .23, -.1);
    if (beat % 2 === 0 || energy > .72) add('hat', 0, beat % 4 === 2 ? .095 : .042, beat % 2 ? .055 : .085, beat % 4 < 2 ? -.35 : .35);
    if (beat % 2 === 0) add('pluck', chord.notes[ARP[beat / 2]] + 12, tick * 2.1, .055, beat % 4 ? -.48 : .48);
    if (bar % 4 >= 2 && beat % 2 === 0) {
      const degree = MELODIES[phrase][beat / 2];
      if (degree >= 0) add('lead', chord.notes[degree], tick * 3, .072, .12);
    }
    if (energy > .72 && [3, 7, 11, 15].includes(beat)) add('spark', chord.notes[(beat >> 2) % 4] + 24, tick * 1.2, .026, beat < 8 ? -.65 : .65);
    if (bar % 8 === 7 && beat >= 13) add('snare', 0, .08, .075 + (beat - 13) * .028, .2);
  } else {
    if (beat === 0) add('bass', chord.bass, tick * 7, scene === 'result' ? .075 : .055);
    if ([0, 6, 10, 14].includes(beat)) add('pluck', chord.notes[ARP[Math.floor(beat / 2)]] + 12, tick * 3, .043, beat < 8 ? -.5 : .5);
    if (scene === 'countdown' && beat % 4 === 0) add('kick', 0, .24, .18);
  }
  return notes;
}

// A small bounded synth graph. Audio-clock scheduling stays separate from the
// visual frame loop; every source and its private nodes are released onended.
export class MusicSynth {
  constructor(context, destination) {
    this.context = context; this.destination = destination; this.voices = new Set();
    this.totalVoices = 0; this.maxVoices = 96;
    this.noise = context.createBuffer(1, context.sampleRate, context.sampleRate);
    const data = this.noise.getChannelData(0); let seed = 0x5f3759df;
    for (let i = 0; i < data.length; i++) { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; data[i] = seed / 0x80000000 - 1; }
    this.echoNodes = []; this.resetEcho();
  }
  resetEcho() {
    for (const n of this.echoNodes) n.disconnect();
    this.echo = this.context.createGain(); this.echo.gain.value = .22;
    const delay = this.context.createDelay(1), second = this.context.createDelay(1), gain = this.context.createGain();
    delay.delayTime.value = MUSIC.stepSeconds * 3; second.delayTime.value = MUSIC.stepSeconds * 3; gain.gain.value = .42;
    this.echo.connect(delay); delay.connect(this.destination); delay.connect(second); second.connect(gain); gain.connect(this.destination);
    this.echoNodes = [this.echo, delay, second, gain];
  }
  voice(source, nodes, time, duration) {
    if (this.voices.size >= this.maxVoices) { for (const n of [source, ...nodes]) n.disconnect(); return; }
    const voice = { source, nodes }; this.voices.add(voice); this.totalVoices++;
    source.onended = () => { source.disconnect(); for (const n of nodes) n.disconnect(); this.voices.delete(voice); };
    source.start(time); source.stop(time + duration);
  }
  play(event, time) {
    const ctx = this.context, { instrument, note, duration, velocity, pan } = event;
    const start = Math.max(ctx.currentTime, time), end = start + duration;
    const env = ctx.createGain(), panner = ctx.createStereoPanner(); panner.pan.value = pan || 0;
    env.connect(panner); panner.connect(this.destination);
    if (['pluck', 'lead', 'spark'].includes(instrument)) panner.connect(this.echo);
    const attack = instrument === 'pad' ? .3 : instrument === 'lead' ? .025 : .004;
    env.gain.setValueAtTime(.00001, start); env.gain.linearRampToValueAtTime(velocity, start + Math.min(attack, duration * .2));
    if (instrument === 'pad') env.gain.setValueAtTime(velocity * .72, start + duration * .65);
    env.gain.exponentialRampToValueAtTime(.00001, end);
    if (instrument === 'snare' || instrument === 'hat') {
      const noise = ctx.createBufferSource(), filter = ctx.createBiquadFilter(); noise.buffer = this.noise;
      filter.type = instrument === 'hat' ? 'highpass' : 'bandpass'; filter.frequency.value = instrument === 'hat' ? 6900 : 1900; filter.Q.value = .7;
      noise.connect(filter); filter.connect(env); this.voice(noise, [filter, env, panner], start, duration + .01); return;
    }
    const osc = ctx.createOscillator();
    if (instrument === 'kick') {
      osc.type = 'sine'; osc.frequency.setValueAtTime(145, start); osc.frequency.exponentialRampToValueAtTime(43, start + .11);
      osc.connect(env); this.voice(osc, [env, panner], start, duration + .01); return;
    }
    const filter = ctx.createBiquadFilter(); filter.type = 'lowpass'; filter.Q.value = .7;
    osc.type = instrument === 'spark' ? 'sine' : instrument === 'pad' ? 'triangle' : 'sawtooth';
    osc.frequency.value = midiHz(note);
    if (instrument === 'pad') osc.detune.value = (pan || 0) * 9;
    const cutoff = instrument === 'bass' ? 720 : instrument === 'pad' ? 1100 : instrument === 'lead' ? 2200 : 3700;
    filter.frequency.setValueAtTime(cutoff, start);
    filter.frequency.exponentialRampToValueAtTime(instrument === 'bass' ? 140 : instrument === 'pad' ? 800 : 600, end);
    osc.connect(filter); filter.connect(env); this.voice(osc, [filter, env, panner], start, duration + .015);
  }
  stop() {
    for (const voice of this.voices) { try { voice.source.stop(); } catch {} voice.source.disconnect(); for (const n of voice.nodes) n.disconnect(); }
    this.voices.clear(); this.resetEcho();
  }
  dispose() { this.stop(); for (const n of this.echoNodes) n.disconnect(); this.echoNodes = []; }
}
