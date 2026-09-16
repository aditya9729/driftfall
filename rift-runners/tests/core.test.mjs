import test from 'node:test';import assert from 'node:assert/strict';
import { Run, STEP, COURSE_LENGTH, course, random, cleanSeed, dailySeed, segmentBox } from '../src/core.js';
export function complete(mode='race'){
 const r=new Run({mode});
 for(let i=0;i<23000&&r.status==='running';i++){
  const g=r.gates.find(g=>!g.passed);
  r.step({x:(g?.x||0)/8,y:(g?.y||0)/4,target:true,fire:true,boost:r.energy>60});
 }
 return r;
}
test('same seed gives identical course; different seed changes it',()=>{assert.deepEqual(course('X'),course('X'));assert.notDeepEqual(course('X'),course('Y'));});
test('seed and mode normalization are bounded',()=>{assert.equal(cleanSeed('<script>_x'),'SCRIPTX');assert.equal(cleanSeed('x'.repeat(90)).length,32);assert.equal(new Run({mode:'bad'}).mode,'race');});
test('daily course uses UTC not client local date',()=>assert.equal(dailySeed(new Date('2026-09-15T23:30:00-04:00')),'DAILY-2026-09-16'));
test('PRNG is repeatable and stays in [0,1)',()=>{const a=random(8),b=random(8);for(let i=0;i<1000;i++){const n=a();assert.equal(n,b());assert.ok(n>=0&&n<1);}});
test('swept collision catches a thin box crossed between steps',()=>{assert.ok(Math.abs(segmentBox({x:0,y:0,z:0},{x:0,y:0,z:10},{x:0,y:0,z:5},.1)-.49)<1e-9);});
test('parallel segment outside box does not hit',()=>assert.equal(segmentBox({x:2,y:0,z:0},{x:2,y:0,z:10},{x:0,y:0,z:5},1),null));
test('untrusted non-finite input cannot poison state',()=>{const r=new Run();r.step({x:NaN,y:Infinity,aimX:NaN,aimY:Infinity,fire:true});assert.ok(Number.isFinite(r.player.x));assert.ok(Number.isFinite(r.bullets[0].x));});
test('invalid timestep rejected rather than skipping collisions',()=>{const r=new Run();for(const dt of [0,-1,NaN,1])assert.throws(()=>r.step({},dt),RangeError);});
test('movement is bounded and follows requested direction',()=>{const r=new Run();for(let i=0;i<600;i++)r.step({x:1,y:1});assert.equal(r.player.x,8);assert.equal(r.player.y,4);});
test('releasing input decelerates instead of sticking',()=>{const r=new Run();for(let i=0;i<100;i++)r.step({x:1});for(let i=0;i<240;i++)r.step({});assert.ok(Math.abs(r.player.vx)<.001);});
test('boost is faster, consumes finite energy and recovers',()=>{const a=new Run(),b=new Run();for(let i=0;i<240;i++){a.step({boost:true});b.step({});}assert.ok(a.distance>b.distance);assert.ok(a.energy<100&&a.energy>=0);const e=a.energy;for(let i=0;i<100;i++)a.step({});assert.ok(a.energy>e);});
test('continuous fire triggers overheating and cools down',()=>{const r=new Run();let hot=false;for(let i=0;i<2400;i++){r.step({fire:true});hot ||= r.overheated;}assert.ok(hot);const shots=r.shots;for(let i=0;i<600;i++)r.step({});assert.equal(r.overheated,false);assert.equal(r.shots,shots);assert.equal(r.heat,0);});
test('bullets actually damage and destroy a target',()=>{const r=new Run();r.entities=[{id:999,type:'drone',x:0,y:0,z:40,size:.8,hp:2,dead:false,collided:false}];for(let i=0;i<180;i++)r.step({fire:true});assert.equal(r.entities[0].dead,true);assert.equal(r.kills,1);assert.ok(r.score>=250);});
test('one shot hits nearest solid target only',()=>{const r=new Run();r.entities=[20,21].map((z,i)=>({id:i,type:'block',x:0,y:0,z,size:1,hp:3,dead:false}));r.step({fire:true});for(let i=0;i<20;i++)r.step({});assert.equal(r.hitCount,1);assert.equal(r.entities[0].hp,2);assert.equal(r.entities[1].hp,3);});
test('collision damages once and invulnerability prevents repeated damage',()=>{const r=new Run();r.entities=[{id:1,type:'block',x:0,y:0,z:5,size:1,hp:3,dead:false}];for(let i=0;i<150;i++)r.step({});assert.equal(r.health,80);r.damage(20);const h=r.health;r.damage(20);assert.equal(r.health,h);});
test('centered gate scores once and gives perfect bonus',()=>{const r=new Run();r.entities=[];r.gates=[{id:0,x:0,y:0,z:2,radius:2.65,passed:false}];for(let i=0;i<240;i++)r.step({});assert.equal(r.gateCount,1);assert.equal(r.perfects,1);assert.equal(r.score,300);});
test('a missed gate breaks the chain',()=>{const r=new Run();r.entities=[];r.combo=4;r.gates=[{id:0,x:8,y:4,z:2,radius:2.65,passed:false}];for(let i=0;i<240;i++)r.step({});assert.equal(r.combo,0);});
test('race journey reaches a real terminal state; restart has clean state',()=>{const r=complete();assert.equal(r.status,'won');assert.ok(r.distance>=COURSE_LENGTH);assert.ok(r.gateCount>30);const t=r.elapsed;r.step({fire:true});assert.equal(r.elapsed,t);assert.equal(new Run().score,0);});
test('survival timing ends at 90 seconds in a collision-free fixture',()=>{const r=new Run({mode:'swarm'});r.entities=[];for(let i=0;i<11000&&r.status==='running';i++)r.step({});assert.equal(r.status,'won');assert.ok(r.elapsed>=90&&r.elapsed<90.02);});
test('zero hull produces loss, not a fake finish',()=>{const r=new Run();r.health=0;r.step({});assert.equal(r.status,'lost');});
test('fixed input replay is deterministic across full runs',()=>{const a=new Run(),b=new Run();for(let i=0;i<4000;i++){const input={x:Math.sin(i*.01),y:0,fire:i%20<10,boost:i%800<400};a.step(input);b.step(input);}assert.deepEqual(a.snapshot(),b.snapshot());assert.deepEqual(a.samples,b.samples);});
test('snapshot is detached from mutable player state',()=>{const r=new Run();const s=r.snapshot();s.player.x=100;assert.equal(r.player.x,0);});
