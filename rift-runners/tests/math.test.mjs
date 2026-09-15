import test from'node:test';import assert from'node:assert/strict';
import{multiply,perspective,lookAt,project,unprojectPlane}from'../src/math.js';
test('pointer unprojection matches tilted-camera target projection',()=>{const vp=multiply(perspective(1.1,1.7,.2,380),lookAt([2,4,13],[.5,0,-30]));for(const[x,y]of[[0,0],[8,4],[-8,-4],[3,-2]]){const p=project(vp,x,y,-55),q=unprojectPlane(vp,p.x,p.y,-55);assert.ok(Math.abs(x-q.x)<1e-5);assert.ok(Math.abs(y-q.y)<1e-5);}});
test('points behind camera are identified',()=>{const vp=multiply(perspective(1,1,.2,380),lookAt([0,0,10],[0,0,-30]));assert.equal(project(vp,0,0,20).visible,false);assert.equal(project(vp,0,0,-20).visible,true);});
