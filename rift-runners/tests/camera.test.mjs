import test from'node:test';import assert from'node:assert/strict';
import{CameraSession,cameraMessage}from'../src/camera.js';
function mock({capture}={}){
 let stops=0,calls=0,terminated=0,constraints,worker;
 const track={stop(){stops++},addEventListener(){}};
 const stream={getTracks:()=>[track],getVideoTracks:()=>[track]};
 const video={srcObject:null,play:async()=>{},pause(){},readyState:4,currentTime:1};
 const errors=[];
 const session=new CameraSession({video,onError:e=>errors.push(e),env:{secure:()=>true,supported:()=>true,
  capture:async c=>{calls++;constraints=c;return capture?capture():stream},
  worker:()=>{worker={postMessage(){queueMicrotask(()=>worker.onmessage({data:{type:'ready'}}));},terminate(){terminated++}};return worker;},
  raf:()=>1,cancelRaf:()=>{},now:()=>100,bitmap:async()=>({close(){}})
 }});
 return{session,video,stream,errors,get stops(){return stops},get calls(){return calls},get terminated(){return terminated},get constraints(){return constraints},get worker(){return worker}};
}
test('camera cannot start without explicit consent',async()=>{const m=mock();await assert.rejects(m.session.start(),/consent/);assert.equal(m.calls,0);});
test('camera request never includes microphone and caps resolution',async()=>{const m=mock();assert.equal(await m.session.start({consent:true}),true);assert.equal(m.constraints.audio,false);assert.equal(m.constraints.video.width.ideal,640);m.session.stop();});
test('stop releases tracks, video and worker, and is idempotent',async()=>{const m=mock();await m.session.start({consent:true});m.session.stop();m.session.stop();assert.equal(m.stops,1);assert.equal(m.terminated,1);assert.equal(m.video.srcObject,null);assert.equal(m.session.active,false);});
test('late permission resolution after cancel does not leak a camera',async()=>{let resolve;const m=mock({capture:()=>new Promise(r=>resolve=r)});const promise=m.session.start({consent:true});m.session.stop();resolve(m.stream);assert.equal(await promise,false);assert.equal(m.stops,1);assert.equal(m.session.active,false);assert.equal(m.video.srcObject,null);});
test('permission denial leaves no active resources',async()=>{const m=mock({capture:()=>{throw new DOMException('denied','NotAllowedError')}});assert.equal(await m.session.start({consent:true}),false);assert.equal(m.session.active,false);assert.equal(m.errors.length,1);assert.match(cameraMessage(m.errors[0]),/denied/);});
test('inference error shuts down camera and worker',async()=>{const m=mock();await m.session.start({consent:true});m.worker.onmessage({data:{type:'error',message:'failed'}});assert.equal(m.stops,1);assert.equal(m.terminated,1);assert.equal(m.errors.length,1);});
test('new session disposes old one rather than acquiring duplicate streams',async()=>{const m=mock();await m.session.start({consent:true});await m.session.start({consent:true});assert.equal(m.stops,1);m.session.stop();assert.equal(m.stops,2);});
test('insecure origin rejected before capture',async()=>{const m=mock();m.session.env.secure=()=>false;await assert.rejects(m.session.start({consent:true}),/HTTPS/);assert.equal(m.calls,0);});
test('worker startup failure closes the already acquired stream',async()=>{const m=mock();m.session.env.worker=()=>{throw new Error('blocked')};assert.equal(await m.session.start({consent:true}),false);assert.equal(m.stops,1);});
