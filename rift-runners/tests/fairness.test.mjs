import test from 'node:test';import assert from 'node:assert/strict';
import { Run } from '../src/core.js';
import { competent } from './pilot.mjs';

function fly(seed,extra={}){
 const run=new Run({seed,...extra});
 for(let i=0;i<26000&&run.status==='running';i++){run.step(competent(run));run.drainEvents();}
 return run;
}
const SEEDS=['NEBULA-01','SEED-0','SEED-3','SEED-7','SEED-11','SEED-19','DAILY-2026-01-01','X'];

test('a pilot who dodges and manages heat finishes every course',()=>{
 // Difficulty must not hinge on one lucky layout. A crude gate-follower loses
 // on almost every seed, which made NEBULA-01-only balance figures misleading;
 // competent play has to succeed broadly or the course generator is unfair.
 const lost=[];
 for(const seed of SEEDS){
  const run=fly(seed);
  if(run.status!=='won')lost.push(`${seed} (${Math.round(run.distance)}/${run.length})`);
 }
 assert.deepEqual(lost,[],`competent play should finish every seed; failed: ${lost.join(', ')}`);
});

test('but flying the gate line without dodging does not',()=>{
 // The other half of the curve: if careless play also won, the lane would be
 // decoration rather than a threat.
 let survived=0;
 for(const seed of SEEDS){
  const run=new Run({seed});
  for(let i=0;i<26000&&run.status==='running';i++){
   const g=run.gates.find(g=>!g.passed);
   run.step({x:(g?.x||0)/8,y:(g?.y||0)/4,target:true,fire:true,boost:run.energy>60});
  }
  if(run.status==='won')survived++;
 }
 assert.ok(survived<SEEDS.length/2,`careless play should usually fail, won ${survived}/${SEEDS.length}`);
});

test('every pilot can finish a course, not just the forgiving one',()=>{
 for(const id of ['vesper','kite','bastion','ember']){
  const run=fly('SEED-3',{character:id});
  assert.equal(run.status,'won',`${id} can finish SEED-3`);
 }
});

test('a custom flight stays winnable at its extremes',()=>{
 // The builder lets a player make a course far denser than the default; it may
 // be brutal, but it must not be impossible.
 for(const flight of [{mix:'seekers',density:1,spread:1},{mix:'pylons',density:1,spread:1},
                      {mix:'balanced',density:1.5,spread:1.2}]){
  const run=fly('SEED-7',{flight});
  assert.equal(run.status,'won',`${JSON.stringify(flight)} is winnable`);
 }
});
