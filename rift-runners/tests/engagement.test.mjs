import test from 'node:test';import assert from 'node:assert/strict';
import { Run, course, RULESET } from '../src/core.js';
import { validateReplay } from '../src/replay.js';

// Fly the gate line, the route a competent player takes.
function fly(run,{fire=true,steps=26000}={}){
 const seen=[];
 for(let i=0;i<steps&&run.status==='running';i++){
  const g=run.gates.find(g=>!g.passed);
  run.step({x:(g?.x||0)/8,y:(g?.y||0)/4,target:true,fire});
  for(const e of run.drainEvents())seen.push(e);
 }
 return seen;
}
// Share of hostiles close enough to the nearest gate to be a real encounter.
function laneShare(c){
 const hostiles=c.entities.filter(e=>e.type!=='cell');
 const near=hostiles.filter(e=>{
  const g=c.gates.reduce((b,g)=>Math.abs(g.z-e.z)<Math.abs(b.z-e.z)?g:b,c.gates[0]);
  return Math.abs(e.x-g.x)<=e.size+.48&&Math.abs(e.y-g.y)<=e.size+.48;
 }).length;
 return near/hostiles.length;
}

test('hostiles sit on the flight line, not scattered to the periphery',()=>{
 // The old spread left ~9% in the lane, so a gate run met almost nothing.
 for(const seed of ['NEBULA-01','X','DAILY-2026-01-01']){
  const share=laneShare(course(seed));
  assert.ok(share>.18,`${seed}: only ${(share*100).toFixed(1)}% of hostiles are in the lane`);
 }
});

test('flying the gate line without firing is fatal',()=>{
 // Hostiles must be a threat you answer, not scenery you drift past.
 const run=new Run({seed:'NEBULA-01'});
 fly(run,{fire:false});
 assert.equal(run.status,'lost');
 assert.ok(run.distance<run.length,'the run ended before the finish');
});

test('firing down the same line survives and clears the objective',()=>{
 // ...and answering the threat has to be winnable, or the lane is just a wall.
 const run=new Run({seed:'NEBULA-01'});
 fly(run,{fire:true});
 assert.equal(run.status,'won');
 assert.ok(run.health>0);
 assert.ok(run.kills>=run.objectiveTarget,'the objective is reachable on the gate line');
});

test('a hull hit reports what the HUD needs to show it',()=>{
 // A contact used to be a silent 0.2s shake, which read as "no collisions".
 const run=new Run({seed:'NEBULA-01'});
 const hits=fly(run,{fire:false}).filter(e=>e.type==='damage');
 assert.ok(hits.length>0,'contact damage actually fires');
 for(const h of hits){
  assert.equal(typeof h.amount,'number');
  assert.ok(h.amount>0);
  assert.ok(['drone','block',null].includes(h.kind));
 }
});

test('the ruleset bump retires ghosts recorded on the old layout',()=>{
 // Courses changed shape, so an older ghost must be rejected, not replayed wrong.
 assert.ok(RULESET>=2);
 for(const stale of [1,RULESET-1,RULESET+1]){
  assert.throws(()=>validateReplay({format:'driftfall-ghost',version:stale,seed:'NEBULA-01',
   mode:'race',status:'won',score:1,elapsed:1,samples:[]}),/compatible/,`v${stale} rejected`);
 }
});
