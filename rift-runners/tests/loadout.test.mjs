import test from 'node:test';import assert from 'node:assert/strict';
import { Run, course, CHARACTERS, DEFAULT_CHARACTER, DEFAULT_FLIGHT, character,
  cleanFlight, isDefaultFlight, flightTag, LANE_X } from '../src/core.js';
import { record, validateReplay, parseReplay, GhostStore, isBetter, lastDistance } from '../src/replay.js';

function fly(run,{fire=true,steps=26000}={}){
 for(let i=0;i<steps&&run.status==='running';i++){
  const g=run.gates.find(g=>!g.passed);
  run.step({x:(g?.x||0)/8,y:(g?.y||0)/4,target:true,fire});
  run.drainEvents();
 }
 return run;
}
class MemStore{constructor(){this.map={}}getItem(k){return k in this.map?this.map[k]:null}
 setItem(k,v){this.map[k]=String(v)}removeItem(k){delete this.map[k]}}

test('the default loadout generates the course it always did',()=>{
 // Any drift here silently rebalances every existing seed.
 assert.deepEqual(course('NEBULA-01','race',DEFAULT_FLIGHT),course('NEBULA-01'));
 assert.equal(character(undefined).id,DEFAULT_CHARACTER);
 for(const k of ['speed','agility','hull','cool','fire'])assert.equal(CHARACTERS.vesper[k],1,`vesper ${k}`);
 assert.ok(isDefaultFlight(DEFAULT_FLIGHT));
 assert.equal(flightTag(DEFAULT_FLIGHT,DEFAULT_CHARACTER),'STD');
});

test('hostile mix selects which enemies the course fields',()=>{
 const only=(mix,type)=>{
  const c=course('NEBULA-01','race',{...DEFAULT_FLIGHT,mix});
  const h=c.entities.filter(e=>e.type!=='cell');
  assert.ok(h.length>0);
  assert.ok(h.every(e=>e.type===type),`${mix} fields only ${type}`);
 };
 only('seekers','drone');only('pylons','block');
 const balanced=course('NEBULA-01','race',{...DEFAULT_FLIGHT,mix:'balanced'}).entities.filter(e=>e.type!=='cell');
 assert.ok(balanced.some(e=>e.type==='drone')&&balanced.some(e=>e.type==='block'));
});

test('density and spread change the course in the direction they promise',()=>{
 const light=course('NEBULA-01','race',{...DEFAULT_FLIGHT,density:.5});
 const heavy=course('NEBULA-01','race',{...DEFAULT_FLIGHT,density:2});
 assert.ok(heavy.hostileTotal>light.hostileTotal*1.5,'density raises hostile count');
 const spread=x=>{
  const c=course('NEBULA-01','race',{...DEFAULT_FLIGHT,spread:x});
  const h=c.entities.filter(e=>e.type!=='cell');
  return h.reduce((m,e)=>Math.max(m,Math.abs(e.x)),0);
 };
 assert.ok(spread(1.6)>spread(.5),'spread widens the lane');
 // Out-of-range values are clamped, never trusted.
 const wild=cleanFlight({mix:'nonsense',density:99,spread:-4});
 assert.equal(wild.mix,'balanced');assert.ok(wild.density<=2&&wild.spread>=.5);
});

test('every pilot is playable and their tradeoffs are real',()=>{
 for(const c of Object.values(CHARACTERS)){
  const run=fly(new Run({seed:'NEBULA-01',character:c.id}));
  assert.ok(['won','lost'].includes(run.status),`${c.id} reaches a terminal state`);
  assert.equal(run.maxHealth,Math.round(100*c.hull));
  assert.ok(run.health<=run.maxHealth,`${c.id} never exceeds its own hull`);
 }
 // BASTION carries more hull than KITE; that is the whole trade.
 assert.ok(new Run({character:'bastion'}).maxHealth>new Run({character:'kite'}).maxHealth);
});

test('a lost run still produces a local ghost to race',()=>{
 // Ghosts used to save only on a win, so most players never saw one at all.
 const run=fly(new Run({seed:'NEBULA-01'}),{fire:false});
 assert.equal(run.status,'lost');
 const ghost=record(run);
 const safe=validateReplay(ghost,{local:true});
 assert.equal(safe.status,'lost');
 assert.ok(lastDistance(safe)>0);
 const store=new GhostStore(new MemStore());
 assert.equal(store.save(ghost),true);
 const back=store.load('NEBULA-01','race',DEFAULT_FLIGHT,DEFAULT_CHARACTER);
 assert.equal(back.status,'lost');
});

test('an unfinished run is never accepted as a shared ghost file',()=>{
 // Local convenience must not weaken what a file from someone else may claim.
 const lost=record(fly(new Run({seed:'NEBULA-01'}),{fire:false}));
 assert.throws(()=>parseReplay(JSON.stringify(lost)),/completed/);
 assert.throws(()=>validateReplay(lost),/completed/);
});

test('a finished run always outranks a best attempt',()=>{
 const won={status:'won',mode:'race',elapsed:70,score:1,samples:[[0,0,0,0],[1,2100,0,0]]};
 const lost={status:'lost',mode:'race',elapsed:40,score:9e6,samples:[[0,0,0,0],[1,900,0,0]]};
 assert.equal(isBetter(won,lost),true);
 assert.equal(isBetter(lost,won),false);
 const further={...lost,samples:[[0,0,0,0],[1,1500,0,0]]};
 assert.equal(isBetter(further,lost),true,'further attempt wins');
 assert.equal(isBetter(lost,further),false);
});

test('ghosts are keyed to their course build, so settings never cross',()=>{
 const store=new GhostStore(new MemStore());
 const base=record(fly(new Run({seed:'NEBULA-01'}),{fire:true}));
 store.save(base);
 const custom={...DEFAULT_FLIGHT,mix:'seekers'};
 // Same seed, different build: the standard ghost must not be handed back.
 assert.ok(store.load('NEBULA-01','race',DEFAULT_FLIGHT,DEFAULT_CHARACTER));
 assert.equal(store.load('NEBULA-01','race',custom,DEFAULT_CHARACTER),null);
 assert.equal(store.load('NEBULA-01','race',DEFAULT_FLIGHT,'ember'),null);
 assert.notEqual(flightTag(custom,DEFAULT_CHARACTER),flightTag(DEFAULT_FLIGHT,DEFAULT_CHARACTER));
});

test('a recorded ghost carries its build, and unknown fields are dropped',()=>{
 const run=fly(new Run({seed:'NEBULA-01',character:'ember',flight:{mix:'seekers',density:1,spread:1}}));
 const ghost=record(run);
 assert.equal(ghost.character,'ember');
 assert.equal(ghost.flight.mix,'seekers');
 const safe=validateReplay({...ghost,evil:'x',character:'not-a-pilot'},{local:true});
 assert.equal(safe.evil,undefined,'unknown properties are never preserved');
 assert.equal(safe.character,DEFAULT_CHARACTER,'an unknown pilot falls back');
});
