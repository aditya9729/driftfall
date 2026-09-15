import test from 'node:test';
import assert from 'node:assert/strict';
import { AudioBus, loadAudioSettings } from '../src/audio.js';
import { MUSIC, MusicSynth, scoreStep, midiHz } from '../src/music.js';
class Param {
  constructor(){this.value=0;this.events=[];}
  setValueAtTime(value,time){this.value=value;this.events.push(['set',value,time]);}
  linearRampToValueAtTime(value,time){this.value=value;this.events.push(['linear',value,time]);}
  exponentialRampToValueAtTime(value,time){this.value=value;this.events.push(['exp',value,time]);}
  setTargetAtTime(value,time,constant){this.value=value;this.events.push(['target',value,time,constant]);}
  cancelScheduledValues(time){this.events.push(['cancel',time]);}
}
class Node {
  constructor(){for(const key of ['gain','pan','frequency','Q','detune','delayTime','threshold','knee','ratio','attack','release'])this[key]=new Param();this.connections=[];this.disconnected=false;}
  connect(other){this.connections.push(other);return other;}
  disconnect(){this.connections=[];this.disconnected=true;}
  start(time){this.startTime=time;}
  stop(time){this.stopTime=time??0;}
}
class Context {
  static instances=[];
  constructor(){this.state='suspended';this.currentTime=0;this.sampleRate=8000;this.destination=new Node();this.nodes=[];this.resumes=0;this.suspends=0;Context.instances.push(this);}
  node(){const n=new Node();this.nodes.push(n);return n;}
  createGain(){return this.node();}createOscillator(){return this.node();}createStereoPanner(){return this.node();}
  createBiquadFilter(){return this.node();}createBufferSource(){return this.node();}createDynamicsCompressor(){return this.node();}createDelay(){return this.node();}
  createBuffer(channels,length){const data=new Float32Array(length);return{getChannelData:()=>data};}
  async resume(){this.resumes++;this.state='running';}
  async suspend(){this.suspends++;this.state='suspended';}
  async close(){this.state='closed';}
}
function setup(options={}){
  let next=1;const intervals=new Map(),saved=new Map();
  const clock={setInterval:fn=>{const id=next++;intervals.set(id,fn);return id;},clearInterval:id=>intervals.delete(id),setTimeout:fn=>{queueMicrotask(fn);return 1;}};
  const storage={getItem:k=>saved.get(k)||null,setItem:(k,v)=>saved.set(k,v)};
  return{bus:new AudioBus({Context,clock,storage,...options}),intervals,saved,clock,storage};
}

test('score has a stable original tempo and A4 tuning',()=>{assert.equal(MUSIC.bpm,116);assert.equal(midiHz(69),440);assert.equal(MUSIC.stepSeconds,60/116/4);});
test('score is deterministic and fresh event objects cannot change later calls',()=>{const a=scoreStep(0),b=scoreStep(0);assert.deepEqual(a,b);a[0].note=1;assert.notEqual(scoreStep(0)[0].note,1);});
test('hangar is drum-free; flight adds kick, snare and hats',()=>{const flatten=scene=>Array.from({length:16},(_,i)=>scoreStep(i,{scene})).flat();assert.ok(flatten('hangar').every(e=>!['kick','snare','hat'].includes(e.instrument)));for(const name of ['kick','snare','hat'])assert.ok(flatten('flight').some(e=>e.instrument===name));});
test('boost adds a sparkle/cymbal layer rather than changing tempo',()=>{const a=scoreStep(3,{scene:'flight',intensity:0}),b=scoreStep(3,{scene:'flight',intensity:1});assert.ok(b.length>a.length);assert.ok(b.some(e=>e.instrument==='spark'));});
test('every arranged event over 64 bars has finite, bounded parameters',()=>{for(const scene of ['hangar','flight','result','countdown'])for(let i=0;i<1024;i++)for(const e of scoreStep(i,{scene,intensity:1})){for(const k of ['note','duration','velocity','pan'])assert.ok(Number.isFinite(e[k]));assert.ok(e.duration>0&&e.duration<5);assert.ok(e.velocity>0&&e.velocity<1);assert.ok(Math.abs(e.pan)<=1);}});
test('invalid score inputs stay bounded and deterministic',()=>{for(const n of [NaN,Infinity,-30])assert.deepEqual(scoreStep(n),scoreStep(0));});
test('page construction and volume edits never create an audio context or autoplay',()=>{const{bus}=setup();bus.setVolume('music',.25);assert.equal(bus.context,null);assert.equal(bus.enabled,false);assert.equal(bus.snapshot().schedulerActive,false);});
test('only independent levels are saved; reload never remembers enabled sound',async()=>{const a=setup();a.bus.setVolume('music',.27);a.bus.setVolume('effects',.83);await a.bus.setEnabled(true);const b=setup({storage:a.storage});assert.deepEqual(b.bus.settings,{music:.27,effects:.83});assert.equal(b.bus.enabled,false);assert.equal(b.bus.context,null);assert.deepEqual(Object.keys(JSON.parse([...a.saved.values()][0])).sort(),['effects','music']);await a.bus.dispose();});
test('malformed, absent and unavailable storage do not break sound defaults',()=>{for(const s of [null,{getItem:()=>'{bad'},{getItem:()=>{throw Error('disabled');}},{getItem:()=>JSON.stringify({music:'loud',effects:null})}])assert.deepEqual(loadAudioSettings(s),{music:.48,effects:.65});});
test('volumes clamp, reject NaN and do not alter another channel',()=>{const{bus}=setup();bus.setVolume('music',12);assert.equal(bus.settings.music,1);bus.setVolume('music',NaN);assert.equal(bus.settings.music,1);bus.setVolume('effects',-1);assert.equal(bus.settings.effects,0);bus.setVolume('__proto__',0);assert.equal(bus.settings.music,1);});
test('enabling creates one context and one audio-clock scheduler',async()=>{const{bus,intervals}=setup();await bus.setEnabled(true);assert.equal(bus.context.state,'running');assert.equal(intervals.size,1);assert.ok(bus.synth.voices.size>0);await bus.setEnabled(true);assert.equal(intervals.size,1);await bus.dispose();assert.equal(intervals.size,0);});
test('mute releases voices, clears timer and suspends context',async()=>{const{bus,intervals}=setup();await bus.setEnabled(true);bus.tone(200,.2);assert.ok(bus.snapshot().activeVoices>0);await bus.setEnabled(false);assert.equal(bus.context.state,'suspended');assert.equal(intervals.size,0);assert.equal(bus.snapshot().activeVoices,0);});
test('pause and resume preserve preference but never retain overlapping schedulers',async()=>{const{bus,intervals}=setup();await bus.setEnabled(true);await bus.suspend();assert.equal(bus.enabled,true);assert.equal(intervals.size,0);await bus.resume();assert.equal(intervals.size,1);await bus.dispose();});
test('resume while disabled cannot create or start audio',async()=>{const{bus}=setup();await bus.suspend();await bus.resume();assert.equal(bus.context,null);assert.equal(bus.enabled,false);});
test('rapid overlapping enable/mute/resume honors the latest decision',async()=>{const{bus,intervals}=setup();await Promise.all([bus.setEnabled(true),bus.setEnabled(false),bus.setEnabled(true)]);assert.equal(bus.enabled,true);assert.equal(bus.context.state,'running');assert.equal(intervals.size,1);await bus.dispose();});
test('late resume resolution cannot resurrect muted audio',async()=>{let release;class Deferred extends Context{resume(){this.state='running';return new Promise(resolve=>release=resolve);}}const{bus,intervals}=setup({Context:Deferred});const enabling=bus.setEnabled(true);await bus.setEnabled(false);release();await enabling;assert.equal(bus.enabled,false);assert.equal(intervals.size,0);assert.equal(bus.context.state,'suspended');});
test('resume rejection produces a usable silent fallback',async()=>{class Rejected extends Context{async resume(){throw Error('browser policy');}}const{bus,intervals}=setup({Context:Rejected});assert.equal(await bus.setEnabled(true),false);assert.match(bus.error,/browser policy/);assert.equal(intervals.size,0);});
test('a long stall skips missed notes instead of scheduling a catch-up burst',async()=>{const{bus}=setup();await bus.setEnabled(true);bus.context.currentTime=90;const before=bus.scheduledSteps;bus.schedule();assert.ok(bus.scheduledSteps-before<=2);assert.ok(bus.nextTime>90);await bus.dispose();});
test('music at zero suppresses new musical voices while effects still work',async()=>{const{bus}=setup();bus.setVolume('music',0);await bus.setEnabled(true);assert.equal(bus.synth.totalVoices,0);bus.tone(400,.1);assert.equal(bus.fx.size,1);bus.setVolume('effects',0);bus.tone(400,.1);assert.equal(bus.fx.size,1);await bus.dispose();});
test('invalid or excessive effect calls cannot allocate unbounded voices',async()=>{const{bus}=setup();await bus.setEnabled(true);bus.tone(NaN,Infinity);assert.equal(bus.fx.size,0);for(let i=0;i<1000;i++)bus.tone(300,.1);assert.equal(bus.fx.size,32);await bus.dispose();});
test('synth caps queued voices and disconnects its graph on disposal',()=>{const ctx=new Context(),synth=new MusicSynth(ctx,ctx.destination);for(let i=0;i<200;i++)synth.play({instrument:'pad',note:60,duration:2,velocity:.1,pan:0},0);assert.equal(synth.voices.size,96);synth.dispose();assert.equal(synth.voices.size,0);assert.equal(synth.echoNodes.length,0);});
test('source-ended cleanup removes the source and its private connections',()=>{const ctx=new Context(),synth=new MusicSynth(ctx,ctx.destination);synth.play(scoreStep(0)[0],0);const voice=[...synth.voices][0];voice.source.onended();assert.equal(synth.voices.size,0);assert.ok(voice.source.disconnected);assert.ok(voice.nodes.every(n=>n.disconnected));synth.dispose();});
test('scene/intensity mapping cannot leak non-finite values to the sequencer',()=>{const{bus}=setup();bus.setScene('playing',Infinity);assert.equal(bus.scene,'flight');assert.equal(bus.intensity,0);bus.setScene('menu',1);assert.equal(bus.scene,'hangar');});
