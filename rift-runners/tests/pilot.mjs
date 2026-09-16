// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// A shared reference pilot. Not a test file: importing a node:test module runs
// its tests, which is exactly how this once contaminated a generated payload.
// A pilot that actually plays: aims for the next gate, slides around a hostile
// about to be in its face, stops firing before it overheats, and does not boost
// into traffic. Everything a person does without being told to.
export function competent(run){
 const g=run.gates.find(g=>!g.passed);
 let tx=g?.x??0, ty=g?.y??0;
 const p=run.player;
 let threat=null,best=Infinity;
 for(const e of run.entities){
  if(e.dead||e.type==='cell')continue;
  const dz=e.z-run.distance;
  if(dz<2||dz>26)continue;
  const off=Math.hypot(e.x-p.x,(e.y-p.y)*1.6);
  if(off<e.size+1.9&&dz<best){best=dz;threat=e;}
 }
 if(threat){
  const dx=p.x-threat.x, dy=p.y-threat.y;
  if(Math.abs(dx)>=Math.abs(dy)) tx=Math.max(-7.6,Math.min(7.6,threat.x+(dx>=0?3.4:-3.4)));
  else ty=Math.max(-3.6,Math.min(3.6,threat.y+(dy>=0?2.4:-2.4)));
 }
 return {x:tx/8,y:ty/4,target:true,fire:run.heat<.82,boost:run.energy>55&&!threat};
}
