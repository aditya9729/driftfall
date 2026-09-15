import test from 'node:test';import assert from 'node:assert/strict';
import { Run, course, WAVE_GROUP, ENEMY_NAMES } from '../src/core.js';

// Fly the gate line (the survivable route) and collect every emitted event.
function fly(run,{steps=26000,fire=true,boost=false}={}){
 const seen=[];
 for(let i=0;i<steps&&run.status==='running';i++){
  const g=run.gates.find(g=>!g.passed);
  run.step({x:(g?.x||0)/8,y:(g?.y||0)/4,target:true,fire,boost});
  for(const e of run.drainEvents())seen.push(e);
 }
 return seen;
}

test('wave grouping adds no rng draws, so seeded courses stay identical',()=>{
 // Byte-identical layout for a seed is what saved ghosts depend on.
 assert.deepEqual(course('NEBULA-01'),course('NEBULA-01'));
 const c=course('NEBULA-01');
 for(const e of c.entities)assert.equal(typeof e.wave,'number');
});

test('waves cover every hostile, exclude cells, and are ordered',()=>{
 const c=course('NEBULA-01');
 const hostiles=c.entities.filter(e=>e.type!=='cell');
 assert.equal(c.waves.reduce((n,w)=>n+w.count,0),hostiles.length);
 assert.equal(c.hostileTotal,hostiles.length);
 assert.ok(c.waves.length>0);
 for(let i=1;i<c.waves.length;i++)assert.ok(c.waves[i].z>=c.waves[i-1].z,'waves ascend by distance');
 for(const w of c.waves)assert.equal(w.seekers+w.pylons,w.count);
 // Cells are pickups, never counted as hostiles to destroy.
 assert.ok(c.entities.some(e=>e.type==='cell'));
});

test('each wave is announced exactly once, in order, even while boosting',()=>{
 const run=new Run({seed:'NEBULA-01'});
 const waves=fly(run,{boost:true}).filter(e=>e.type==='wave');
 // Boosting can cross several markers in one step; announcements must still be
 // one-per-wave, in order, and only for waves actually reached.
 const reached=run.waves.filter(w=>w.z<=run.distance).length;
 assert.equal(waves.length,reached,'no duplicate or skipped announcements');
 waves.forEach((w,i)=>assert.equal(w.index,i));
 assert.ok(waves.length>1,'more than one wave was announced');
 // Every wave sits inside the course, so a full flight announces all of them.
 for(const w of run.waves)assert.ok(w.z<=run.length);
 assert.equal(run.snapshot().wave.total,run.waves.length);
});

test('a wave is announced before its first hostile is in range',()=>{
 const c=course('NEBULA-01');
 for(const w of c.waves){
  const first=Math.min(...c.entities.filter(e=>e.type!=='cell'&&e.wave===w.index).map(e=>e.z));
  assert.ok(w.z<first,'announcement leads the hostiles');
 }
});

test('objective target is bounded and deterministic',()=>{
 const a=course('NEBULA-01'),b=course('NEBULA-01');
 assert.equal(a.objectiveTarget,b.objectiveTarget);
 for(const seed of ['NEBULA-01','X','DAILY-2026-01-01']){
  for(const mode of ['race','swarm']){
   const c=course(seed,mode);
   assert.ok(c.objectiveTarget>=3&&c.objectiveTarget<=40,`${seed}/${mode} target in range`);
   assert.ok(c.objectiveTarget<=c.hostileTotal);
  }
 }
});

test('objective completes once, pays the bonus, and reports progress',()=>{
 const run=new Run({seed:'NEBULA-01'});
 const done=fly(run,{fire:true}).filter(e=>e.type==='objective');
 assert.ok(run.kills>0,'the flight actually destroyed hostiles');
 if(run.objectiveDone){
  assert.equal(done.length,1,'the objective fires exactly once');
  assert.equal(done[0].target,run.objectiveTarget);
  const s=run.snapshot();
  assert.equal(s.objective.progress,s.objective.target);
  assert.equal(s.objective.done,true);
 }else{
  assert.equal(done.length,0);
  assert.ok(run.snapshot().objective.progress<run.objectiveTarget);
 }
});

test('snapshot exposes wave and objective without dropping existing fields',()=>{
 const s=new Run({seed:'NEBULA-01'}).snapshot();
 for(const key of ['seed','mode','status','elapsed','distance','length','player','health','score','kills'])assert.ok(key in s,key);
 assert.deepEqual(s.wave,{current:0,total:course('NEBULA-01').waves.length});
 assert.equal(s.objective.progress,0);
 assert.equal(s.objective.done,false);
});

test('hostile display names stay stable for the HUD and briefing',()=>{
 assert.equal(ENEMY_NAMES.drone,'SEEKER');
 assert.equal(ENEMY_NAMES.block,'PYLON');
 assert.equal(WAVE_GROUP,4);
});
