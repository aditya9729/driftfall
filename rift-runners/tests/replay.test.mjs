import test from'node:test';import assert from'node:assert/strict';
import {Run}from'../src/core.js';import{record,parseReplay,validateReplay,ghostAt,GhostStore,MAX_REPLAY_BYTES}from'../src/replay.js';
const r=new Run();for(let i=0;i<20000&&r.status==='running';i++){const g=r.gates.find(g=>!g.passed);r.step({target:true,x:(g?.x||0)/8,y:(g?.y||0)/4,fire:true,boost:true});}
const ghost=record(r);
test('a real completed run round-trips through validated ghost format',()=>{assert.equal(r.status,'won');assert.deepEqual(parseReplay(JSON.stringify(ghost)),ghost);});
test('ghost interpolation follows samples and ends with the recording',()=>{const p=ghostAt(ghost,10);assert.ok(p.distance>0);assert.equal(ghostAt(ghost,ghost.elapsed+1),null);});
test('prototype keys and unknown properties are removed',()=>{const v=validateReplay({...ghost,arbitrary:'ignored'});assert.equal(v.arbitrary,undefined);});
test('oversized ghost rejected before JSON parsing',()=>assert.throws(()=>parseReplay(' '.repeat(MAX_REPLAY_BYTES+1)),/large/));
test('non-finite, reversed-time and impossible movement samples rejected',()=>{for(const sample of [[1,NaN,0,0],[-1,0,0,0],[1,0,900,0]]){const bad=structuredClone(ghost);bad.samples[4]=sample;assert.throws(()=>validateReplay(bad));}});
test('partial, wrong-ruleset and failed ghosts are rejected',()=>{assert.throws(()=>validateReplay({...ghost,status:'lost'}));assert.throws(()=>validateReplay({...ghost,version:20}));assert.throws(()=>validateReplay({...ghost,samples:ghost.samples.slice(0,4)}));});
test('storage denial does not break the run',()=>{const store=new GhostStore({getItem(){throw Error('denied')},setItem(){throw Error('denied')}});assert.equal(store.load(ghost.seed,ghost.mode),null);assert.equal(store.save(ghost),false);assert.equal(store.available,false);});
test('storage does not let mismatched seed run on the selected course',()=>{const store=new GhostStore({getItem:()=>JSON.stringify(ghost)});assert.equal(store.load('OTHER','race'),null);});
