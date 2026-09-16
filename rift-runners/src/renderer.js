// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// Dependency-free WebGL2. One shared cube mesh, bounded instancing, procedural sky.
import { clamp, lerp, random } from './core.js';
import { multiply, perspective, lookAt, project, unprojectPlane } from './math.js';
import { ghostAt } from './replay.js';
const VS = `#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec3 center;
layout(location=3) in vec3 scale;
layout(location=4) in vec2 rotation;
layout(location=5) in vec4 tint;
uniform mat4 vp;
out vec3 n; out vec3 world; out vec4 color;
void main(){
 float cz=cos(rotation.x),sz=sin(rotation.x),cy=cos(rotation.y),sy=sin(rotation.y);
 mat3 rz=mat3(cz,sz,0.,-sz,cz,0.,0.,0.,1.);
 mat3 ry=mat3(cy,0.,-sy,0.,1.,0.,sy,0.,cy);
 mat3 r=ry*rz;
 world=center+r*(position*scale); n=r*normal; color=tint;
 gl_Position=vp*vec4(world,1.);
}`;
const FS = `#version 300 es
precision highp float;
in vec3 n; in vec3 world; in vec4 color;
uniform vec3 fogColor; uniform float glow;
out vec4 frag;
void main(){
 float lit=.26+.72*max(dot(normalize(n),normalize(vec3(-.35,.8,.6))),0.);
 vec3 c=color.rgb*(lit+color.a*1.7);
 float fog=1.-exp(-max(0.,-world.z)*.007);
 c=mix(c,fogColor,min(.92,fog));
 if(glow>.5) c=color.rgb*.07;
 c=c/(vec3(1.)+c*.4);
 frag=vec4(pow(c,vec3(.88)),1.);
}`;
const SKY_VS = `#version 300 es
precision highp float;
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,.999,1.);}`;
const SKY_FS = `#version 300 es
precision highp float;
in vec2 uv;
uniform vec2 resolution; uniform float time; uniform vec3 skyTint; uniform vec3 accent;
out vec4 frag;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
void main(){
 float aspect=resolution.x/resolution.y;
 vec2 p=vec2(uv.x*aspect,uv.y);
 vec3 c=mix(vec3(.008,.018,.025),skyTint*.25,pow(uv.y,.6));
 float cloud=sin(uv.x*5.+uv.y*7.)*.5+.5;
 c+=accent*.027*cloud*exp(-abs(uv.y-.53)*4.);
 vec2 cell=floor(p*240.); vec2 sub=fract(p*240.)-.5;
 float h=hash(cell);
 float star=(1.-smoothstep(0.,.14,length(sub)))*step(.984,h);
 c+=vec3(.6,.76,.75)*star*(.6+.4*sin(time*.4+h*120.));
 vec2 q=p-vec2(aspect*.76,.72);
 float r=.145;
 float ring=length(vec2(q.x*.91+q.y*.42,(-q.x*.42+q.y*.91)*3.3));
 float rings=(1.-smoothstep(.295,.30,ring))*smoothstep(.21,.22,ring);
 float d=length(q);
 c+=accent*.24*rings*(.65+.35*sin(ring*500.));
 if(d<r){
   vec3 n=vec3(q/r,sqrt(max(0.,1.-d*d/(r*r))));
   float l=max(0.,dot(n,normalize(vec3(-.75,.4,.7))));
   float terrain=.90+.1*sin(q.x*180.+sin(q.y*120.)*2.);
   c=mix(skyTint*.2,accent*.47,pow(l,1.3))*terrain;
   c+=accent*.25*pow(1.-n.z,3.);
 }
 c+=accent*.05*exp(-max(0.,d-r)*80.)*step(r,d);
 float vignette=1.-.35*pow(length((uv-.5)*1.3),1.5);
 c*=vignette;
 c+=vec3((hash(gl_FragCoord.xy)-.5)*.009);
 frag=vec4(c,1.);
}`;
export const BIOMES = [
  { name: 'THE FRACTURE', label: 'Orbital salvage belt', sky: [.035,.09,.13], accent: [.49,.92,.83], block: [.13,.22,.26], edge: [.68,.96,.44] },
  { name: 'VIOLET WAKE', label: 'The glass moon', sky: [.09,.045,.15], accent: [.72,.52,.99], block: [.22,.15,.3], edge: [.98,.63,.41] },
  { name: 'SUNKEN SUN', label: 'A star in pieces', sky: [.13,.07,.045], accent: [.98,.57,.32], block: [.28,.19,.12], edge: [.99,.9,.55] }
];
function shader(gl, type, source) {
  const s = gl.createShader(type); gl.shaderSource(s, source); gl.compileShader(s);
  if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) { const error = gl.getShaderInfoLog(s); gl.deleteShader(s); throw new Error(error); }
  return s;
}
function program(gl, vertex, fragment) {
  const p = gl.createProgram(), vs = shader(gl, gl.VERTEX_SHADER, vertex), fs = shader(gl, gl.FRAGMENT_SHADER, fragment);
  gl.attachShader(p, vs); gl.attachShader(p, fs); gl.linkProgram(p); gl.deleteShader(vs); gl.deleteShader(fs);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
  return p;
}
function cubeVertices() {
  const data = [];
  const faces = [
    [[1,0,0],[[.5,-.5,-.5],[.5,.5,-.5],[.5,.5,.5],[.5,-.5,.5]]],
    [[-1,0,0],[[-.5,-.5,.5],[-.5,.5,.5],[-.5,.5,-.5],[-.5,-.5,-.5]]],
    [[0,1,0],[[-.5,.5,-.5],[-.5,.5,.5],[.5,.5,.5],[.5,.5,-.5]]],
    [[0,-1,0],[[-.5,-.5,.5],[-.5,-.5,-.5],[.5,-.5,-.5],[.5,-.5,.5]]],
    [[0,0,1],[[.5,-.5,.5],[.5,.5,.5],[-.5,.5,.5],[-.5,-.5,.5]]],
    [[0,0,-1],[[-.5,-.5,-.5],[-.5,.5,-.5],[.5,.5,-.5],[.5,-.5,-.5]]]
  ];
  for (const [n, v] of faces) for (const i of [0,1,2,0,2,3]) data.push(...v[i], ...n);
  return new Float32Array(data);
}
export class Renderer {
  constructor(canvas) {
    this.canvas = canvas;
    const gl = canvas.getContext('webgl2', { alpha: false, antialias: true, powerPreference: 'high-performance' });
    this.gl = gl; this.ctx = gl ? null : canvas.getContext('2d', { alpha: false });
    if (!gl && !this.ctx) throw new Error('Neither WebGL 2 nor Canvas 2D is available. Try a current browser.');
    this.maxInstances = 7000; this.instances = new Float32Array(this.maxInstances * 12);
    if (gl) {
      this.main = program(gl, VS, FS); this.sky = program(gl, SKY_VS, SKY_FS);
      this.vao = gl.createVertexArray(); gl.bindVertexArray(this.vao);
      this.mesh = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, this.mesh); gl.bufferData(gl.ARRAY_BUFFER, cubeVertices(), gl.STATIC_DRAW);
      for (let i = 0; i < 2; i++) { gl.enableVertexAttribArray(i); gl.vertexAttribPointer(i, 3, gl.FLOAT, false, 24, i * 12); }
      this.instanceBuffer = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, this.instanceBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, this.instances.byteLength, gl.DYNAMIC_DRAW);
      for (const [loc, size, offset] of [[2,3,0],[3,3,12],[4,2,24],[5,4,32]]) {
        gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 48, offset); gl.vertexAttribDivisor(loc, 1);
      }
      gl.bindVertexArray(null);
      this.uniform = Object.fromEntries(['vp','fogColor','glow'].map(n => [n, gl.getUniformLocation(this.main,n)]));
      this.skyUniform = Object.fromEntries(['resolution','time','skyTint','accent'].map(n => [n, gl.getUniformLocation(this.sky,n)]));
    }
    this.count = 0; this.particles = []; this.shake = 0; this.frameTimes = [];
    this.stats = { drawCalls: 0, instances: 0, triangles: 0, frameMs: 0, renderMs: 0, backend: gl ? 'WebGL2' : 'Canvas2D compatibility' };
    this.vp = perspective(1, 1, .1, 500); this.lastFrame = 0;
    const rng = random(88217); this.stars = Array.from({length:220},()=>[rng(),rng(),rng()]);
    this.resize();
  }
  resize() {
    const ratio = Math.min(window.devicePixelRatio || 1, this.gl ? 1.6 : 1);
    const w = Math.max(1, Math.floor(this.canvas.clientWidth * ratio));
    const h = Math.max(1, Math.floor(this.canvas.clientHeight * ratio));
    if (this.canvas.width !== w || this.canvas.height !== h) { this.canvas.width = w; this.canvas.height = h; }
  }
  box(x,y,z,sx,sy,sz,color,emission=0,rz=0,ry=0) {
    if (this.count >= this.maxInstances) return;
    const o = this.count++ * 12, b = this.instances;
    b[o]=x;b[o+1]=y;b[o+2]=z;b[o+3]=sx;b[o+4]=sy;b[o+5]=sz;b[o+6]=rz;b[o+7]=ry;
    b[o+8]=color[0];b[o+9]=color[1];b[o+10]=color[2];b[o+11]=emission;
  }
  ring(x,y,z,r,color,time,large=false) {
    const segments = large ? 36 : 24;
    for (let j=0;j<segments;j++) {
      const a=j/segments*Math.PI*2;
      this.box(x+Math.cos(a)*r,y+Math.sin(a)*r,z,.17,2*Math.PI*r/segments*.87,.32,color,.8,a);
      if (large && j%3===0) this.box(x+Math.cos(a)*(r+.32),y+Math.sin(a)*(r+.32),z,.55,.28,.7,[.16,.23,.24],0,a);
    }
  }
  ship(x,y,z,roll,time,ghost=false,scale=1) {
    const white=ghost?[.55,.38,.9]:[.78,.83,.76], dark=ghost?[.28,.2,.44]:[.09,.16,.18], light=ghost?[.83,.48,1]:[.67,1,.46];
    const add=(dx,dy,dz,sx,sy,sz,c,e=0,rz=0)=>{
      const xx=dx*Math.cos(roll)-dy*Math.sin(roll), yy=dx*Math.sin(roll)+dy*Math.cos(roll);
      this.box(x+xx*scale,y+yy*scale,z+dz*scale,sx*scale,sy*scale,sz*scale,c,e,roll+rz);
    };
    add(0,0,0,.85,.40,2.4,white);
    add(0,.04,-1.1,.51,.32,.88,white);
    add(0,.15,-1.64,.22,.18,.48,white);
    add(0,.27,-.45,.55,.24,.75,dark);
    add(0,.40,-.47,.36,.07,.5,light,.45);
    add(0,-.17,.4,.55,.20,1.6,dark);
    for(const side of [-1,1]){
      add(side*.88,-.03,.3,1.25,.14,1.05,white,0,side*.09);
      add(side*1.53,-.02,.65,.3,.22,.95,dark);
      add(side*.98,.0,-.75,.83,.12,.47,white,0,-side*.1);
      add(side*1.50,.1,.38,.07,.09,.55,light,1.2);
      add(side*.5,.06,.99,.35,.4,.7,dark);
      add(side*.5,.06,1.37,.25,.25,.17,light,1.4);
      add(side*.5,.06,1.70,.14,.14,.6+Math.sin(time*31)*.15,light,1.8);
    }
  }
  effects(events, run) {
    for (const e of events) {
      if (e.type === 'damage') this.shake = .2;
      if (!['destroy','hit','gate','cell','damage'].includes(e.type)) continue;
      const color=e.type==='damage'?[1,.34,.2]:e.type==='gate'?[.65,1,.4]:e.type==='destroy'?[1,.57,.3]:[.55,.95,1];
      const rng=random(`${e.t}-${e.type}`), n=e.type==='destroy'?22:e.type==='hit'?4:12;
      for(let i=0;i<n&&this.particles.length<260;i++) this.particles.push({
        x:e.x??run.player.x,y:e.y??run.player.y,z:e.z??run.distance,
        vx:(rng()-.5)*11,vy:(rng()-.5)*11,vz:(rng()-.5)*10,
        life:.5+rng()*.5,max:1,size:.06+rng()*.17,color
      });
    }
  }
  draw(run, time, { menu=false, reducedMotion=false, ghost=null, dt=.016 }={}) {
    const started=performance.now(); this.resize(); const gl=this.gl;
    const w=this.canvas.width,h=this.canvas.height,aspect=w/h;
    const distance=menu?time*7:run.distance;
    const section=menu?0:clamp(distance/700,0,2.999);
    const index=Math.floor(section), next=Math.min(2,index+1), blend=clamp((section-index-.75)*4,0,1);
    const biome=BIOMES[index], sky=biome.sky.map((v,i)=>lerp(v,BIOMES[next].sky[i],blend));
    const accent=biome.accent.map((v,i)=>lerp(v,BIOMES[next].accent[i],blend));
    const edge=biome.edge.map((v,i)=>lerp(v,BIOMES[next].edge[i],blend));
    if (gl) {
      gl.viewport(0,0,w,h); gl.disable(gl.DEPTH_TEST);gl.disable(gl.CULL_FACE);gl.disable(gl.BLEND);
      gl.useProgram(this.sky); gl.bindVertexArray(null);
      gl.uniform2f(this.skyUniform.resolution,w,h);gl.uniform1f(this.skyUniform.time,reducedMotion?0:time);
      gl.uniform3fv(this.skyUniform.skyTint,sky);gl.uniform3fv(this.skyUniform.accent,accent);gl.drawArrays(gl.TRIANGLES,0,3);
      gl.clear(gl.DEPTH_BUFFER_BIT);
    } else this.drawSoftwareSky(sky, accent, time);
    const px=run.player.x,py=run.player.y;
    this.shake=Math.max(0,this.shake-dt);
    const jitter=reducedMotion?0:Math.sin(time*87)*this.shake*.4;
    const cameraX=menu?-3.5:px*.23+jitter, cameraY=menu?5.0:3.6+py*.16;
    const fov=(menu?58:61+(run.boosting&&!reducedMotion?5:0))*Math.PI/180;
    this.eye=[cameraX,cameraY,menu?18:13];
    this.vp=multiply(perspective(fov,aspect,.2,380),lookAt(this.eye,[menu?3.2:px*.15,menu?.4:py*.12,-30]));
    this.count=0;
    // Deterministic scenery windows: bounded draw work independent of run length.
    const base=Math.floor(distance/12);
    for(let j=-2;j<24;j++){
      const row=base+j, z=distance-row*12;
      if(z>27) continue;
      const rng=random(row+48291);
      for(const side of [-1,1]){
        const rise=3+rng()*9, x=side*(11+rng()*5);
        this.box(x,-5+rise*.5,z,3.8,rise,5+rng()*5,biome.block,0,(rng()-.5)*.09);
        this.box(x-side*1.94,-4.8+rise*.7,z,.07,.16,4.7,edge,.8);
        for(let k=0;k<3;k++){
          const by=rise-3+rng()*5;
          this.box(side*(18+rng()*13),by,z-rng()*12,2+rng()*5,2+rng()*7,4+rng()*5,biome.block,0,(rng()-.5)*.3);
        }
        this.box(side*9.4,-5.4,z,.12,.1,9,accent,.5);
        if(row%4===0){
          this.box(side*7.5,7.7,z,4,.35,.7,biome.block);
          this.box(side*9.5,4.3,z,.4,7,.7,biome.block);
          this.box(side*7.5,7.5,z,3,.06,.2,edge,.7);
        }
      }
      for(let k=-2;k<=2;k++){
        this.box(k*3.8,-6.1-rng()*.7,z,3.5,.5,9.5,biome.block);
        if((row+k)%3===0)this.box(k*3.8,-5.81,z,.06,.04,5,accent,.8);
      }
    }
    if(menu){
      this.ring(5,1,-29,9,accent,time,true);
      this.ring(5,1,-34,8.5,edge,time,true);
      this.ring(5,1,-44,7.4,accent,time,true);
      this.ship(5,Math.sin(time*.65)*.25+.2,0,-.2+Math.sin(time*.4)*.07,time,false,1.75);
      const rng=random(712);
      for(let i=0;i<22;i++){
        const x=rng()*27-10,y=rng()*17-3,z=-12-rng()*90;
        this.box(x,y,z,.3+rng()*.9,.4+rng()*.9,.7,biome.block,.15,time*.07+i,time*.1+i);
      }
    }else{
      for(const g of run.gates){
        const z=distance-g.z;
        if(z>8||z< -270)continue;
        const c=g.passed?(g.collected?edge:[.27,.25,.24]):accent;
        this.ring(g.x,g.y,z,g.radius,c,time);
        // Sparse outer notches distinguish the gate from the surrounding world.
        this.box(g.x,g.y+g.radius+.28,z,.35,.12,.45,edge,1);
      }
      for(const e of run.entities){
        const z=distance-e.z;
        if(e.dead||z>9||z< -230)continue;
        // Hostiles must read against a near-black sky through fog, so their hulls
        // are emissive and each wears a bracket while it is still far enough to
        // answer. Four boxes, not a ring: the instance budget feeds the ship too.
        const bracket=(c,r)=>{
          const p=Math.max(.35,1-(-z)/230)*.9;
          this.box(e.x-r,e.y,z,.13,r*1.1,.12,c,p);this.box(e.x+r,e.y,z,.13,r*1.1,.12,c,p);
          this.box(e.x,e.y-r,z,r*1.1,.13,.12,c,p);this.box(e.x,e.y+r,z,r*1.1,.13,.12,c,p);
        };
        if(e.type==='drone'){
          // The central hull matches the simulation collision box; fins are decorative.
          this.box(e.x,e.y,z,e.size*2,e.size*2,e.size*2,[.93,.31,.26],.75,time*.7);
          this.box(e.x,e.y,z+.86,.95,.28,.07,[1,.86,.52],2.4);
          this.box(e.x-1.12,e.y,z,.56,.21,.88,[1,.52,.3],.9,time*.7);
          this.box(e.x+1.12,e.y,z,.56,.21,.88,[1,.52,.3],.9,-time*.7);
          bracket([1,.42,.3],e.size*2.2);
        }else if(e.type==='block'){
          this.box(e.x,e.y,z,e.size*2,e.size*2,e.size*2,[.66,.68,.52],.45);
          this.box(e.x,e.y,z+e.size+.01,2,.13,.07,[1,.77,.33],1.9);
          this.box(e.x,e.y,z+e.size+.04,.13,2,.07,[1,.77,.33],1.9);
          bracket([1,.73,.36],e.size*1.9);
        }else{
          this.box(e.x,e.y,z,.65,.65,.65,[.65,1,.52],1,Math.PI/4,time);
          this.ring(e.x,e.y,z,.92,[.65,1,.52],time);
        }
      }
      for(const b of run.bullets){
        const z=distance-b.z;
        this.box(b.x-.12,b.y,z,.06,.06,2.7,edge,2);
        this.box(b.x+.12,b.y,z,.06,.06,2.7,edge,2);
      }
      const g=ghostAt(ghost,run.elapsed);
      if(g&&Math.abs(distance-g.distance)<220)this.ship(g.x,g.y,distance-g.distance,0,time,true,.85);
      this.ship(px,py,0,clamp(-run.player.vx*.036,-.4,.4),time);
      if(run.invulnerable>0){
        this.ring(px,py,-.2,2.1,[1,.45,.3],time);
      }
      if(run.boosting&&!reducedMotion){
        for(let i=0;i<18;i++){
          const a=i*2.4,z=-((time*85+i*12)%150);
          this.box(Math.cos(a)*8,Math.sin(a)*5,z,.025,.025,3.8,accent,.4);
        }
      }
    }
    for(const p of this.particles){
      p.life-=dt; p.x+=p.vx*dt;p.y+=p.vy*dt;p.z+=p.vz*dt;
      if(p.life>0)this.box(p.x,p.y,distance-p.z,p.size,p.size,p.size,p.color,1,p.life*5,p.life*4);
    }
    this.particles=this.particles.filter(p=>p.life>0);
    if (gl) {
      gl.enable(gl.DEPTH_TEST); gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK);
      gl.useProgram(this.main);gl.bindVertexArray(this.vao);gl.bindBuffer(gl.ARRAY_BUFFER,this.instanceBuffer);
      gl.bufferSubData(gl.ARRAY_BUFFER,0,this.instances.subarray(0,this.count*12));
      gl.uniformMatrix4fv(this.uniform.vp,false,this.vp);gl.uniform3fv(this.uniform.fogColor,sky);gl.uniform1f(this.uniform.glow,0);
      gl.drawArraysInstanced(gl.TRIANGLES,0,36,this.count);gl.bindVertexArray(null);
      this.stats.drawCalls=2;
    } else this.drawSoftwareGeometry(sky);
    this.stats.instances=this.count;this.stats.triangles=this.count*12+1;
    this.stats.renderMs=+(performance.now()-started).toFixed(2);
    if(this.lastFrame){this.frameTimes.push(started-this.lastFrame);if(this.frameTimes.length>90)this.frameTimes.shift();}
    this.lastFrame=started;
    this.stats.frameMs=+(this.frameTimes.reduce((s,n)=>s+n,0)/Math.max(1,this.frameTimes.length)).toFixed(2);
  }
  drawSoftwareSky(sky, accent, time) {
    const c=this.ctx,w=this.canvas.width,h=this.canvas.height;
    const rgb=(v,m=1)=>`rgb(${v.map(x=>Math.round(clamp(x*m,0,1)*255)).join(',')})`;
    const gradient=c.createLinearGradient(0,0,0,h);gradient.addColorStop(0,rgb(sky,1.2));gradient.addColorStop(1,'#081316');
    c.fillStyle=gradient;c.fillRect(0,0,w,h);
    for(const[x,y,b]of this.stars){c.globalAlpha=.3+b*.5;c.fillStyle='#c8e4dd';c.fillRect(x*w,y*h,(b>.85?2:1),(b>.85?2:1));}c.globalAlpha=1;
    const px=w*.76,py=h*.28,r=h*.145;
    c.save();c.translate(px,py);c.rotate(-.38);c.strokeStyle=rgb(accent,.38);c.lineWidth=7;c.beginPath();c.ellipse(0,0,r*2,r*.36,0,0,Math.PI*2);c.stroke();
    c.lineWidth=2;c.strokeStyle=rgb(accent,.7);c.beginPath();c.ellipse(0,0,r*2.15,r*.39,0,0,Math.PI*2);c.stroke();c.restore();
    const pg=c.createRadialGradient(px-r*.3,py-r*.45,1,px,py,r);pg.addColorStop(0,rgb(accent,.42));pg.addColorStop(.8,rgb(sky,1.1));pg.addColorStop(1,rgb(accent,.6));
    c.fillStyle=pg;c.beginPath();c.arc(px,py,r,0,Math.PI*2);c.fill();
    c.save();c.beginPath();c.arc(px,py,r-.5,0,Math.PI*2);c.clip();c.globalAlpha=.11;c.strokeStyle=rgb(accent);
    for(let i=-12;i<12;i++){c.beginPath();c.ellipse(px+i*3,py+i*10,r*1.2,r*.14,-.3,0,Math.PI*2);c.stroke();}c.restore();
  }
  drawSoftwareGeometry(sky) {
    // A true projected 3D compatibility renderer, not a static screenshot.
    // It consumes exactly the same scene instances and camera as WebGL.
    const ctx=this.ctx,w=this.canvas.width,h=this.canvas.height,faces=[],data=this.instances;
    const corners=[[-.5,-.5,-.5],[.5,-.5,-.5],[.5,.5,-.5],[-.5,.5,-.5],[-.5,-.5,.5],[.5,-.5,.5],[.5,.5,.5],[-.5,.5,.5]];
    const sides=[[[4,5,6,7],[0,0,1]],[[1,0,3,2],[0,0,-1]],[[5,1,2,6],[1,0,0]],[[0,4,7,3],[-1,0,0]],[[3,7,6,2],[0,1,0]],[[0,1,5,4],[0,-1,0]]];
    for(let i=0;i<this.count;i++){
      const o=i*12,x=data[o],y=data[o+1],z=data[o+2];if(z< -165||z>24)continue;
      const sz=Math.sin(data[o+6]),cz=Math.cos(data[o+6]),sy=Math.sin(data[o+7]),cy=Math.cos(data[o+7]);
      const rotate=(a,b,c)=>[cy*(cz*a-sz*b)+sy*c,sz*a+cz*b,-sy*(cz*a-sz*b)+cy*c];
      const verts=corners.map(v=>{const q=rotate(v[0]*data[o+3],v[1]*data[o+4],v[2]*data[o+5]);const p=project(this.vp,x+q[0],y+q[1],z+q[2]);return{x:(p.x*.5+.5)*w,y:(.5-p.y*.5)*h,visible:p.visible};});
      if(verts.some(v=>!v.visible))continue;
      if(verts.every(v=>v.x<0)||verts.every(v=>v.x>w)||verts.every(v=>v.y<0)||verts.every(v=>v.y>h))continue;
      for(const[indices,normal]of sides){
        const n=rotate(...normal);if(n[0]*(this.eye[0]-x)+n[1]*(this.eye[1]-y)+n[2]*(this.eye[2]-z)<0)continue;
        const lit=.26+.72*Math.max(0,(-.35*n[0]+.8*n[1]+.6*n[2])/1.059);
        const fog=Math.min(.92,1-Math.exp(-Math.max(0,-z)*.007));
        const color=[0,1,2].map(k=>{let v=data[o+8+k]*(lit+data[o+11]*1.7);v=lerp(v,sky[k],fog);return Math.round(clamp(Math.pow(v/(1+v*.4),.88),0,1)*255);});
        const half=[normal[0]*data[o+3]*.5,normal[1]*data[o+4]*.5,normal[2]*data[o+5]*.5];const offset=rotate(...half);
        const vp=this.vp;const depth=vp[3]*(x+offset[0])+vp[7]*(y+offset[1])+vp[11]*(z+offset[2])+vp[15];
        faces.push({depth,points:indices.map(j=>verts[j]),color:`rgb(${color.join(',')})`});
      }
    }
    faces.sort((a,b)=>b.depth-a.depth);
    for(const f of faces){ctx.fillStyle=f.color;ctx.strokeStyle=f.color;ctx.lineWidth=.45;ctx.beginPath();ctx.moveTo(f.points[0].x,f.points[0].y);for(let i=1;i<4;i++)ctx.lineTo(f.points[i].x,f.points[i].y);ctx.closePath();ctx.fill();ctx.stroke();}
    this.stats.drawCalls=faces.length+1;
  }
  screenPoint(x,y,z=-55){
    const p=project(this.vp,x,y,z);
    return { x:(p.x*.5+.5)*this.canvas.clientWidth,y:(.5-p.y*.5)*this.canvas.clientHeight,visible:p.visible };
  }
  pointerAim(clientX,clientY){
    const rect=this.canvas.getBoundingClientRect();
    const nx=(clientX-rect.left)/rect.width*2-1,ny=1-(clientY-rect.top)/rect.height*2;
    const p=unprojectPlane(this.vp,nx,ny,-55);
    return{x:clamp(p.x,-16,16),y:clamp(p.y,-10,10)};
  }

  dispose(){
    const gl=this.gl;if(!gl)return;gl.deleteBuffer(this.mesh);gl.deleteBuffer(this.instanceBuffer);gl.deleteVertexArray(this.vao);gl.deleteProgram(this.main);gl.deleteProgram(this.sky);
  }
}
