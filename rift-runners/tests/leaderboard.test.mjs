import test from 'node:test';import assert from 'node:assert/strict';
import { Run, COURSE_LENGTH } from '../src/core.js';
import { record } from '../src/replay.js';
import worker from '../../leaderboard/src/worker.js';
import * as client from '../src/leaderboard.js';

const ORIGIN='https://aditya9729.github.io';
// A genuine completed run, flown the way the other fixtures fly.
const won=(()=>{
 const r=new Run();
 for(let i=0;i<23000&&r.status==='running';i++){
  const g=r.gates.find(g=>!g.passed);
  r.step({target:true,x:(g?.x||0)/8,y:(g?.y||0)/4,fire:true,boost:r.energy>60});
 }
 assert.equal(r.status,'won','fixture finishes the route');
 return record(r);
})();
const lost=(()=>{
 const r=new Run();
 for(let i=0;i<23000&&r.status==='running';i++)r.step({target:true,x:0,y:0,fire:false});
 assert.equal(r.status,'lost');
 return record(r);
})();

// Minimal D1 double: enough to exercise every branch the worker takes.
function fakeDB(){
 const rows=[],throttle=[];
 const stmt=sql=>{
  const s={sql,args:[],
   bind(...a){s.args=a;return s;},
   async all(){
    if(/FROM runs/.test(sql)){
     const [seed,mode,tag]=s.args;
     return {results:rows.filter(r=>r.seed===seed&&r.mode===mode&&r.tag===tag)
       .sort((a,b)=>a.elapsed-b.elapsed)};
    }
    return {results:[]};
   },
   async first(){
    if(/COUNT\(\*\)/.test(sql)){const [who,since]=s.args;return {n:throttle.filter(t=>t.who===who&&t.created>since).length};}
    if(/SELECT ghost/.test(sql)){const [id]=s.args;const r=rows.find(r=>r.id===id);return r?{ghost:r.ghost}:null;}
    return null;
   },
   async run(){return {};}
  };
  return s;
 };
 return {
  prepare:stmt,
  async batch(list){
   for(const s of list){
    if(/INSERT INTO runs/.test(s.sql)){
     const [id,seed,mode,tag,pilot,name,elapsed,score,ghost,created]=s.args;
     rows.push({id,seed,mode,tag,pilot,name,elapsed,score,ghost,created});
    }
    if(/INSERT INTO throttle/.test(s.sql)){const [who,created]=s.args;throttle.push({who,created});}
    if(/DELETE FROM throttle/.test(s.sql)){const [before]=s.args;
     for(let i=throttle.length-1;i>=0;i--)if(throttle[i].created<before)throttle.splice(i,1);}
   }
   return [];
  },
  _rows:rows,_throttle:throttle
 };
}
const env=db=>({DB:db,ALLOWED_ORIGIN:ORIGIN});
const post=(body,origin=ORIGIN)=>new Request('https://board.test/submit',
 {method:'POST',headers:{'Content-Type':'application/json','Origin':origin,'CF-Connecting-IP':'203.0.113.9'},
  body:typeof body==='string'?body:JSON.stringify(body)});

test('a real finished run is accepted and ranked',async()=>{
 const db=fakeDB();
 const res=await worker.fetch(post({name:'aditya',ghost:won}),env(db));
 assert.equal(res.status,201);
 const body=await res.json();
 assert.equal(body.rank,1);
 assert.equal(db._rows.length,1);
 assert.equal(db._rows[0].name,'ADITYA','names are normalised for display');
});

test('an unfinished run is refused, exactly as an import would be',async()=>{
 const db=fakeDB();
 const res=await worker.fetch(post({name:'x',ghost:lost}),env(db));
 assert.equal(res.status,422);
 assert.match((await res.json()).error,/completed/i);
 assert.equal(db._rows.length,0,'nothing is stored');
});

test('an impossible finish time is refused',async()=>{
 const db=fakeDB();
 // Physically impossible: the whole route in a few seconds.
 const cheat={...won,elapsed:4,samples:[[0,0,0,0],[3.9,COURSE_LENGTH,0,0]]};
 const res=await worker.fetch(post({name:'x',ghost:cheat}),env(db));
 assert.equal(res.status,422);
 assert.equal(db._rows.length,0);
});

test('a forged score cannot be posted without a valid ghost',async()=>{
 const db=fakeDB();
 for(const ghost of [null,{},{format:'nope'},{...won,samples:[]},{...won,version:1}]){
  const res=await worker.fetch(post({name:'x',ghost}),env(db));
  assert.ok(res.status>=400,'rejected');
 }
 assert.equal(db._rows.length,0);
});

test('only the game origin may write',async()=>{
 const db=fakeDB();
 const res=await worker.fetch(post({name:'x',ghost:won},'https://evil.example'),env(db));
 assert.equal(res.status,403);
 assert.equal(db._rows.length,0);
});

test('oversized bodies are dropped before parsing',async()=>{
 const db=fakeDB();
 const res=await worker.fetch(post(' '.repeat(200_000)),env(db));
 assert.equal(res.status,413);
});

test('submissions are rate limited per submitter',async()=>{
 const db=fakeDB();
 let last;
 for(let i=0;i<8;i++)last=await worker.fetch(post({name:'x',ghost:won}),env(db));
 assert.equal(last.status,429);
 assert.ok(db._rows.length<=6,'the cap actually bounds writes');
});

test('the board reports what it verified, and keeps builds apart',async()=>{
 const db=fakeDB();
 await worker.fetch(post({name:'a',ghost:won}),env(db));
 const shown=await worker.fetch(new Request(`https://board.test/board?seed=${won.seed}&mode=race`,
  {headers:{Origin:ORIGIN}}),env(db));
 const body=await shown.json();
 assert.equal(body.entries.length,1);
 assert.match(body.verified,/validated/i);
 // A different pilot is a different board, never a mixed ranking.
 const other=await worker.fetch(new Request(`https://board.test/board?seed=${won.seed}&mode=race&pilot=ember`,
  {headers:{Origin:ORIGIN}}),env(db));
 assert.equal((await other.json()).entries.length,0);
});

test('the client calls its own origin, so no CSP grant is needed',async()=>{
 // Same-origin by construction: a relative endpoint cannot reach a third party
 // even if someone later edits it carelessly.
 assert.equal(client.available(),true);
 assert.ok(client.ENDPOINT.startsWith('/'),'the endpoint is relative');
 assert.ok(!/^https?:/i.test(client.ENDPOINT),'never an absolute third-party origin');
});

test('the same worker serves at the root and mounted under /api',async()=>{
 // It runs as a standalone Worker and as a same-origin Pages Function.
 for(const base of ['https://board.test','https://driftfall.world/api']){
  const db=fakeDB();
  const res=await worker.fetch(new Request(`${base}/submit`,
   {method:'POST',headers:{'Content-Type':'application/json','Origin':ORIGIN,'CF-Connecting-IP':'203.0.113.9'},
    body:JSON.stringify({name:'a',ghost:won})}),env(db));
  assert.equal(res.status,201,`${base} accepts a submission`);
  const shown=await worker.fetch(new Request(`${base}/board?seed=${won.seed}&mode=race`,
   {headers:{Origin:ORIGIN}}),env(db));
  assert.equal((await shown.json()).entries.length,1,`${base} serves the board`);
 }
});

test('a same-origin submission is accepted without an Origin header',async()=>{
 // Browsers omit Origin on some same-origin requests; that must not read as
 // a hostile cross-origin write.
 const db=fakeDB();
 const res=await worker.fetch(new Request('https://driftfall.world/api/submit',
  {method:'POST',headers:{'Content-Type':'application/json','CF-Connecting-IP':'203.0.113.9'},
   body:JSON.stringify({name:'a',ghost:won})}),env(db));
 assert.equal(res.status,201);
});

test('a foreign origin is still refused on the mounted path',async()=>{
 const db=fakeDB();
 const res=await worker.fetch(new Request('https://driftfall.world/api/submit',
  {method:'POST',headers:{'Content-Type':'application/json','Origin':'https://evil.example','CF-Connecting-IP':'203.0.113.9'},
   body:JSON.stringify({name:'x',ghost:won})}),env(db));
 assert.equal(res.status,403);
 assert.equal(db._rows.length,0);
});
