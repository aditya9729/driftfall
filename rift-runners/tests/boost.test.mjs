import test from 'node:test';import assert from 'node:assert/strict';
import { Run } from '../src/core.js';
import { HandMapper, Reach, Fist, OneEuro, describeHand } from '../src/gestures.js';

function fly(policy,{seed='NEBULA-01'}={}){
 const run=new Run({seed});
 for(let i=0;i<26000&&run.status==='running';i++){
  const g=run.gates.find(g=>!g.passed);
  run.step({x:(g?.x||0)/8,y:(g?.y||0)/4,target:true,fire:true,boost:policy(run)});
  run.drainEvents();
 }
 return run;
}
// 21 landmarks; `scale` moves the hand toward the camera (wider knuckle span).
function points(x=.5,pinch=.6,scale=1){
 const p=[];for(let i=0;i<21;i++)p.push({x,y:.55});
 p[5]={x:x-.06*scale,y:.55};p[17]={x:x+.06*scale,y:.55};
 p[9]={x,y:.55};p[0]={x,y:.6};
 p[4]={x,y:.55};p[8]={x:x+pinch*.12*scale,y:.55};
 return p;
}
const frame=(x=.5,pinch=.6,scale=1)=>({landmarks:[points(x,pinch,scale)],handedness:[[{categoryName:'Right',score:1}]]});
function calibrated(mode='one'){
 const m=new HandMapper({mode});
 for(let i=0;i<12;i++)m.ingest(frame(),i*50);
 assert.ok(m.calibrate(550),'fixture calibrates');
 return m;
}

test('holding boost the whole way is fatal, managing it is fastest',()=>{
 // Boost used to be free speed. It must now be a decision with a downside.
 const reckless=fly(()=>true);
 const managed=fly(r=>r.energy>60);
 const never=fly(()=>false);
 assert.equal(reckless.status,'lost','constant boost overruns the lane');
 assert.equal(managed.status,'won');
 assert.equal(never.status,'won');
 assert.ok(managed.elapsed<never.elapsed,'managed boost finishes sooner');
 assert.ok(managed.score>never.score,'and scores better');
});

test('a gate taken while boosting pays more and is reported',()=>{
 const run=new Run({seed:'NEBULA-01'});
 let boosted=null;
 for(let i=0;i<26000&&run.status==='running'&&!boosted;i++){
  const g=run.gates.find(g=>!g.passed);
  run.step({x:(g?.x||0)/8,y:(g?.y||0)/4,target:true,fire:false,boost:true});
  boosted=run.drainEvents().find(e=>e.type==='gate'&&e.boosted);
 }
 assert.ok(boosted,'a boosted gate happens');
 assert.equal(boosted.boosted,true);
 assert.ok(run.boostGates>0);
 assert.equal(run.snapshot().boostGates,run.boostGates);
});

test('the throttle engages on a deliberate push and releases on retreat',()=>{
 const r=new Reach();
 assert.equal(r.update(1,0),false);
 assert.equal(r.update(1.3,0),false,'needs a dwell, not one frame');
 assert.equal(r.update(1.3,100),true);
 assert.equal(r.update(1.15,150),true,'hysteresis holds through wobble');
 assert.equal(r.update(1.02,200),false);
 assert.equal(r.update(NaN,250),false,'no span, no throttle');
});

test('one hand can boost by pushing toward the camera',()=>{
 // One-hand mode previously had no boost gesture at all, only the keyboard.
 const m=calibrated('one');
 for(let i=0;i<8;i++)m.ingest(frame(.5,.6,1.5),600+i*50);
 const input=m.input(950);
 assert.equal(input.boost,true,'a pushed hand throttles up');
 assert.ok(input.reach>1.2);
 for(let i=0;i<8;i++)m.ingest(frame(.5,.6,1),1000+i*50);
 assert.equal(m.input(1350).boost,false,'and releases when it comes back');
});

test('a resting hand is quieter than the raw signal it is given',()=>{
 // The point of the adaptive filter: jitter at rest must not reach the ship.
 const m=calibrated('one');
 let raw=0,t=600;
 for(let i=0;i<40;i++){
  const jitter=(i%2?1:-1)*.012;       // a steady hand, trembling slightly
  raw=.5+jitter;
  m.ingest(frame(raw),t);t+=33;
 }
 assert.ok(Math.abs(m.input(t).x)<.05,'tremor is absorbed');
});

test('the filter is no laggier than the fixed one it replaced',()=>{
 // beta multiplies the signal's own derivative, so it is signal-scale dependent.
 // A pixel-scale beta on a +/-1 signal made steering 2.2x SLOWER to respond
 // while looking like a smoothing improvement. Pin both ends of the tradeoff.
 const DT=1/30;
 const fixed=()=>{let v=0;return x=>{v+=(x-v)*(1-Math.exp(-DT/.06));return v;};};
 const euro=()=>{const f=new OneEuro();return x=>f.filter(x,DT);};
 const step=make=>{const f=make();for(let i=0;i<30;i++)f(0);
  for(let i=1;i<=150;i++){if(f(.8)>=.72)return i*DT;}return Infinity;};
 const tremor=make=>{const f=make();let peak=0;
  for(let i=0;i<120;i++){const v=f((i%2?1:-1)*.012);if(i>40)peak=Math.max(peak,Math.abs(v));}return peak;};
 assert.ok(step(euro)<=step(fixed),`responds at least as fast (${step(euro).toFixed(3)}s vs ${step(fixed).toFixed(3)}s)`);
 assert.ok(tremor(euro)<tremor(fixed),'and is quieter at rest');
});

test('the filter still follows a real move, and rejects nonsense',()=>{
 const f=new OneEuro();
 let v=0;for(let i=0;i<30;i++)v=f.filter(1,1/30);
 assert.ok(v>.9,'converges on a held value');
 assert.equal(Number.isFinite(f.filter(NaN,1/30)),true,'NaN never poisons state');
 assert.ok(Math.abs(f.filter(NaN,1/30)-v)<.2);
});

test('describeHand exposes the span the throttle depends on',()=>{
 const near=describeHand(points(.5,.6,2)),far=describeHand(points(.5,.6,1));
 assert.ok(near.span>far.span,'closer hand has a wider knuckle span');
 assert.ok(Number.isFinite(far.span)&&far.span>0);
});

// ---- Fist to fire ---------------------------------------------------------
// Landmarks with independent control of finger curl, so the fist and the pinch
// can be posed separately - including the awkward case where both are closed.
function hand({x=.5,curl=1.1,pinch=.6,scale=1}={}){
 const p=new Array(21).fill(null).map(()=>({x,y:.55}));
 const span=.12*scale;
 p[0]={x,y:.62};                                   // wrist
 p[5]={x:x-span/2,y:.55};p[17]={x:x+span/2,y:.55}; // knuckle line defines span
 p[9]={x:x-span/6,y:.55};p[13]={x:x+span/6,y:.55};
 // Each fingertip sits `curl * span` from its own knuckle.
 for(const [tip,knuckle] of [[8,5],[12,9],[16,13],[20,17]])
  p[tip]={x:p[knuckle].x,y:p[knuckle].y-curl*span};
 p[4]={x,y:.55};p[8]={x:p[8].x,y:p[8].y};
 // Thumb tip placed to hit the requested pinch ratio against the index tip.
 p[4]={x:p[8].x+pinch*span,y:p[8].y};
 return p;
}
const shot=(o)=>({landmarks:[hand(o)],handedness:[[{categoryName:'Right',score:1}]]});
function ready(){
 const m=new HandMapper({mode:'one'});
 for(let i=0;i<12;i++)m.ingest(shot(),i*50);
 assert.ok(m.calibrate(550));
 return m;
}

test('curl separates an open hand from a closed one',()=>{
 assert.ok(describeHand(hand({curl:1.2})).curl>1,'fingers out reads high');
 assert.ok(describeHand(hand({curl:.45})).curl<.62,'a fist reads low');
});

test('a fist fires, and opening the hand stops firing',()=>{
 const m=ready();
 let t=600;
 for(let i=0;i<6;i++,t+=50)m.ingest(shot({curl:.45,pinch:.9}),t);
 assert.equal(m.input(t).fire,true,'a closed fist fires');
 assert.equal(m.input(t).grip,'fist');
 for(let i=0;i<6;i++,t+=50)m.ingest(shot({curl:1.2,pinch:.9}),t);
 assert.equal(m.input(t).fire,false,'an open hand holds fire');
});

test('a fist needs a dwell, so a passing curl is not a shot',()=>{
 const f=new Fist();
 assert.equal(f.update(.45,0),false,'one frame is not a gesture');
 assert.equal(f.update(.45,80),true);
 assert.equal(f.update(.7,120),true,'hysteresis rides out a wobble');
 assert.equal(f.update(.85,160),false);
 assert.equal(f.update(NaN,200),false);
});

test('a fist that also closes thumb-to-index still fires exactly once',()=>{
 // The real ambiguity: a real fist often squeezes thumb and index together, so
 // both detectors engage. Firing must stay a clean boolean, not flicker.
 const m=ready();
 let t=600;
 for(let i=0;i<8;i++,t+=50)m.ingest(shot({curl:.45,pinch:.2}),t);
 assert.equal(m.input(t).fire,true);
 // Release the fist but keep pinching: still firing, via the other detector.
 for(let i=0;i<8;i++,t+=50)m.ingest(shot({curl:1.2,pinch:.2}),t);
 assert.equal(m.input(t).fire,true,'pinch keeps firing after the fist opens');
 for(let i=0;i<8;i++,t+=50)m.ingest(shot({curl:1.2,pinch:.9}),t);
 assert.equal(m.input(t).fire,false,'both released stops fire');
});

test('a fist never fires before calibration',()=>{
 const m=new HandMapper({mode:'one'});
 for(let i=0;i<12;i++)m.ingest(shot({curl:.45,pinch:.2}),i*50);
 assert.equal(m.input(550).fire,false);
});

test('a lost hand drops a held fist immediately',()=>{
 const m=ready();
 let t=600;
 for(let i=0;i<6;i++,t+=50)m.ingest(shot({curl:.45}),t);
 assert.equal(m.input(t).fire,true);
 m.ingest({landmarks:[]},t+50);
 assert.equal(m.input(t+50).fire,false,'no weapon left held when tracking drops');
});

test('a fist does not disturb steering or the throttle',()=>{
 const m=ready();
 let t=600;
 // Curl is normalised by span, so closing the hand must not read as a push.
 for(let i=0;i<8;i++,t+=50)m.ingest(shot({curl:.45,x:.5}),t);
 assert.equal(m.input(t).boost,false,'a fist is not a throttle');
 assert.ok(Math.abs(m.input(t).x)<.15,'and does not drag the steering');
});
