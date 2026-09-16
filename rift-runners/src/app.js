// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
import { Run, STEP, clamp, emptyInput, cleanSeed, dailySeed, MODES, CHARACTERS, DEFAULT_CHARACTER,
  DEFAULT_FLIGHT, character, cleanFlight, isDefaultFlight, flightTag, course } from './core.js';
import { Renderer, BIOMES } from './renderer.js';
import { HandMapper } from './gestures.js';
import { CameraSession, cameraMessage } from './camera.js';
import { GhostStore, record, parseReplay, MAX_REPLAY_BYTES, ghostAt, lastDistance } from './replay.js';
import { AudioBus } from './audio.js';
const $ = id => document.getElementById(id);
const canvas=$('world'),audio=new AudioBus();
let renderer;
try { renderer=new Renderer(canvas); document.documentElement.dataset.renderer=renderer.stats.backend; }
catch(error){$('fatal').hidden=false;$('fatal-message').textContent=error.message;}
$('reload').onclick=()=>location.reload();
let storage=null;try{storage=window.localStorage;}catch{}
const store=new GhostStore(storage);
let mode='race',state='menu',inputMode='keyboard',lastInputMode='keyboard';
// Loadout: pilot, course build and presentation. Persisted so a chosen flight
// survives a reload; every value is re-cleaned by core before it is used.
let loadout={character:DEFAULT_CHARACTER,flight:{...DEFAULT_FLIGHT},view:'third',theme:'auto'};
try{const saved=JSON.parse(localStorage.getItem('driftfall.loadout')||'null');
  if(saved)loadout={character:character(saved.character).id,flight:cleanFlight(saved.flight),
    view:saved.view==='first'?'first':'third',
    theme:['fracture','violet','sunken'].includes(saved.theme)?saved.theme:'auto'};
}catch{}
function saveLoadout(){try{localStorage.setItem('driftfall.loadout',JSON.stringify(loadout));}catch{}}
let run=new Run(),ghost=null,importedGhost=null,lastReplay=null;
let mapper=new HandMapper(),inferenceMs=0;
let countdown=3,accumulator=0,lastTime=0,calloutUntil=0,lastHud=0,toastTimer=0;
// Seconds a hand must stay steady before the run starts by itself.
const STEADY_LAUNCH=1.4;let steadySince=0;
let lastSceneDraw=0;
let helpWasPlaying=false,reduced=matchMedia('(prefers-reduced-motion: reduce)').matches;
let activeInput=emptyInput();
const keys=new Set();
const pointer={down:false,aim:false,x:0,y:0,touch:false,tx:0,ty:0,fire:false,boost:false};
const query=new URLSearchParams(location.search);
const challengeSeed=query.has('seed')?cleanSeed(query.get('seed')):null;
if(query.has('mix')||query.has('density')||query.has('spread')){
  loadout.flight=cleanFlight({mix:query.get('mix'),density:query.get('density'),spread:query.get('spread')});
}
if(challengeSeed)$('seed').value=challengeSeed;
function showToast(message){clearTimeout(toastTimer);$('toast').textContent=message;$('toast').hidden=false;toastTimer=setTimeout(()=>$('toast').hidden=true,4500);}
function clearInput(){keys.clear();pointer.down=false;pointer.fire=false;pointer.boost=false;pointer.touch=false;pointer.aim=false;pointer.x=0;pointer.y=0;activeInput=emptyInput();for(const id of ['touch-fire','touch-boost'])$(id).classList.remove('active');}
function currentSeed(){return cleanSeed($('seed').value);}
function selectMode(value){
  mode=MODES.includes(value)?value:'race';
  for(const b of document.querySelectorAll('[data-mode]')){const selected=b.dataset.mode===mode;b.classList.toggle('selected',selected);b.setAttribute('aria-pressed',String(selected));}
  if(mode==='daily')$('seed').value=(query.get('mode')==='daily'&&challengeSeed)?challengeSeed:dailySeed();
  $('seed').readOnly=mode==='daily';updateRecordHint();
}
function updateRecordHint(){
  const g=store.load(currentSeed(),mode,loadout.flight,loadout.character);
  // A ghost used to appear only after a win, so most players never saw one and
  // the HUD just read SOLO FLIGHT forever. Best attempts now count too, and the
  // hint says which kind you have.
  $('record-hint').textContent=!g?'NO GHOST YET · YOUR FIRST RUN MAKES ONE'
    :g.status==='won'?`PERSONAL BEST ${formatTime(g.elapsed)} · GHOST READY`
    :`BEST ATTEMPT ${Math.round(lastDistance(g))} M · GHOST READY`;
}
for(const b of document.querySelectorAll('[data-mode]'))b.onclick=()=>selectMode(b.dataset.mode);
$('seed').onchange=()=>{$('seed').value=currentSeed();updateRecordHint();};
selectMode(query.get('mode')||'race');
function setState(value){
  state=value;audio.setScene(value);document.body.classList.toggle('playing',value!=='menu');
  $('menu').hidden=value!=='menu';$('hud').hidden=value==='menu';
  $('countdown').hidden=value!=='countdown';
}
function formatTime(t){const minutes=Math.floor(t/60),seconds=Math.floor(t%60),tenths=Math.floor((t%1)*10);return `${String(minutes).padStart(2,'0')}:${String(seconds).padStart(2,'0')}.${tenths}`;}
function begin(control='keyboard'){
  clearInput();inputMode=control;lastInputMode=control;
  run=new Run({seed:currentSeed(),mode,flight:loadout.flight,character:loadout.character});
  // An imported ghost must match the course build too, or it is racing a
  // different layout with the same seed.
  const fits=g=>g&&g.seed===run.seed&&g.mode===run.mode&&flightTag(g.flight,g.character)===run.tag;
  ghost=fits(importedGhost)?importedGhost:store.load(run.seed,run.mode,loadout.flight,loadout.character);
  accumulator=0;countdown=3;calloutUntil=0;lastReplay=null;
  if(renderer){renderer.particles=[];renderer.shake=0;}
  $('callout').textContent='';$('countdown').textContent='3';
  $('timer-label').textContent=mode==='swarm'?'SURVIVE':'FLIGHT TIME';
  $('control-strip').textContent=control==='hands'?
    (mapper.mode==='two'?'PILOT HAND · STEER + PINCH BOOST    GUNNER HAND · AIM + PINCH FIRE    SPACE ALSO FIRES    R · RECENTER':'MOVE PALM · STEER    PINCH OR SPACE · FIRE    SHIFT · BOOST    R · RECENTER    P · PAUSE'):
    'WASD / ARROWS · MOVE    SPACE / CLICK · FIRE    SHIFT · BOOST    P · PAUSE';
  $('boost-label').textContent=control==='hands'&&mapper.mode==='two'?'PILOT PINCH / BOOST':'SHIFT / BOOST';
  setState('countdown');canvas.focus();audio.resume();updateHud(performance.now());
}
function toMenu(){
  for(const d of document.querySelectorAll('dialog[open]'))d.close();
  camera.stop();clearInput();setState('menu');audio.resume();syncAudioUI();updateRecordHint();
  $('live-preview').hidden=true; $('setup-preview-slot').append($('tracking-preview'));
}
function pause(reason='Your flight is paused.',tracking=false){
  if(!['playing','countdown'].includes(state))return;
  setState('paused');clearInput();audio.suspend();
  $('pause-title').innerHTML=tracking?'SIGNAL LOST.<br><em>FLIGHT HELD.</em>':'HOLDING<br><em>POSITION.</em>';
  $('pause-reason').textContent=reason+(camera.active?' Your camera is still on; use Stop camera below to turn it off.':'');
  $('pause-keyboard').hidden=inputMode!=='hands';$('pause-stop-camera').hidden=!camera.active;
  $('pause-dialog').showModal();
}
function resume(){
  if(inputMode==='hands'&&(!camera.active||mapper.lost(performance.now()))){$('pause-reason').textContent='Bring the steering hand back into view, or continue with keyboard. The camera can be stopped below.';return;}
  $('pause-dialog').close();accumulator=0;lastTime=performance.now();clearInput();audio.resume();
  setState(countdown>0?'countdown':'playing');canvas.focus();
}
function stopCameraWithFallback(){
  const wasHands=inputMode==='hands';camera.stop();inputMode='keyboard';clearInput();
  $('pause-keyboard').hidden=true;$('pause-stop-camera').hidden=true;
  $('control-strip').textContent='WASD / ARROWS · MOVE    SPACE / CLICK · FIRE    SHIFT · BOOST    P · PAUSE';
  $('boost-label').textContent='SHIFT / BOOST';
  if(wasHands&&['playing','countdown'].includes(state))pause('Camera stopped. Your flight is safe; continue with keyboard or touch.');
  else if(state==='paused')$('pause-reason').textContent='Camera stopped. Resume using keyboard or touch.';
  $('live-preview').hidden=true;
}
const camera=new CameraSession({video:$('camera-video'),
  onStatus(status){
    $('calibration-message').textContent=status==='permission'?'Waiting for your browser’s camera permission…':status==='loading'?'Loading the hand model. Your camera stays local. You can cancel below.':'Show your hand and hold it comfortably still.';
    $('camera-status').hidden=!camera.active;
  },
  onResult(result,now){
    inferenceMs=result.inferenceMs||0;mapper.ingest(result,result.timestamp??now);drawHands(result);
    if($('camera-dialog').open){
      const ready=mapper.canCalibrate(now);$('calibrate').disabled=!ready;
      // Reaching for the button is what breaks the pose, so a steady hand
      // launches on its own. The button stays for anyone who wants it.
      if(!ready)steadySince=0;else if(!steadySince)steadySince=now;
      const held=steadySince?(now-steadySince)/1000:0;
      if(ready&&held>=STEADY_LAUNCH){launchHands();return;}
      $('calibration-message').textContent=ready
        ?`Hand signal steady. Launching in ${Math.max(1,Math.ceil(STEADY_LAUNCH-held))}… hold still.`
        :mapper.mode==='two'?'Keep both hands in view and comfortably still.':'Keep one hand in view and comfortably still.';
    }
  },
  onError(error){
    $('camera-error').textContent=cameraMessage(error);$('calibration-message').textContent='Camera off. You can try again or use keyboard / touch.';
    $('allow-camera').disabled=!$('camera-consent').checked;
    if(['playing','countdown'].includes(state))pause(cameraMessage(error),true);
  },
  onStopped(){$('camera-status').hidden=true;$('calibrate').disabled=true;$('live-preview').hidden=true;}
});
function drawHands(result){
  const overlay=$('hand-overlay');if(overlay.width!==640){overlay.width=640;overlay.height=480;}
  const ctx=overlay.getContext('2d');ctx.clearRect(0,0,640,480);
  const edges=[[0,1],[1,2],[2,3],[3,4],[0,5],[5,6],[6,7],[7,8],[5,9],[9,10],[10,11],[11,12],[9,13],[13,14],[14,15],[15,16],[13,17],[0,17],[17,18],[18,19],[19,20]];
  for(const [index,points]of(result.landmarks||[]).entries()){
    ctx.strokeStyle=index===0?'#d9ff8a':'#a39aff';ctx.fillStyle=ctx.strokeStyle;ctx.lineWidth=3;
    for(const[a,b]of edges){ctx.beginPath();ctx.moveTo((1-points[a].x)*640,points[a].y*480);ctx.lineTo((1-points[b].x)*640,points[b].y*480);ctx.stroke();}
    for(const p of points){ctx.beginPath();ctx.arc((1-p.x)*640,p.y*480,3.5,0,Math.PI*2);ctx.fill();}
  }
}
function setupCamera(){
  camera.stop();$('setup-preview-slot').append($('tracking-preview'));
  $('camera-consent').checked=false;$('allow-camera').disabled=true;
  $('consent-step').hidden=false;$('calibration-step').hidden=true;$('camera-error').textContent='';
  mapper=new HandMapper({mode:$('hand-mode').value,swap:$('swap-hands').checked,sensitivity:Number($('sensitivity').value)});
  $('camera-dialog').showModal();
}
async function allowCamera(){
  if(!$('camera-consent').checked)return;
  $('allow-camera').disabled=true;$('consent-step').hidden=true;$('calibration-step').hidden=false;$('camera-error').textContent='';
  mapper=new HandMapper({mode:$('hand-mode').value,swap:$('swap-hands').checked,sensitivity:Number($('sensitivity').value)});
  $('calibration-tip').textContent=mapper.mode==='two'?'Rest your elbows. Screen-left hand pilots; screen-right hand aims. Hold both still and calibrate. Roles stay locked even if your hands cross.':'Rest your elbows. Hold one hand comfortably in view, keep it still, then calibrate. Small movements are enough.';
  try{await camera.start({consent:true});}catch(error){$('camera-error').textContent=cameraMessage(error);}
}
function cancelSetup(){steadySince=0;camera.stop();$('camera-dialog').close();$('camera-consent').checked=false;}
$('play-hands').onclick=setupCamera;
$('play-keyboard').onclick=()=>{camera.stop();begin('keyboard');};
$('camera-consent').onchange=()=>{$('allow-camera').disabled=!$('camera-consent').checked;};
// The swap-roles control only means anything in two-hand mode.
const syncHandMode=()=>{$('swap-hands-label').hidden=$('hand-mode').value!=='two';};
$('hand-mode').onchange=syncHandMode;syncHandMode();
$('allow-camera').onclick=allowCamera;$('cancel-camera').onclick=cancelSetup;
$('camera-dialog').addEventListener('cancel',event=>{event.preventDefault();cancelSetup();});
for(const id of ['camera-fallback','calibration-fallback'])$(id).onclick=()=>{cancelSetup();begin('keyboard');};
function launchHands(){
  if(!mapper.calibrate(performance.now()))return false;
  steadySince=0;
  $('camera-dialog').close();$('camera-consent').checked=false;
  $('live-preview').append($('tracking-preview'));$('live-preview').hidden=false;
  begin('hands');
  return true;
}
$('calibrate').onclick=launchHands;
$('stop-camera').onclick=stopCameraWithFallback;$('pause-stop-camera').onclick=stopCameraWithFallback;
$('pause-button').onclick=()=>pause();$('resume').onclick=resume;
$('pause-keyboard').onclick=()=>{stopCameraWithFallback();resume();};
$('quit').onclick=toMenu;
$('pause-dialog').addEventListener('cancel',e=>{e.preventDefault();resume();});
function finish(){
  lastInputMode=inputMode;camera.stop();clearInput();setState('result');
  $('result-eyebrow').textContent=run.status==='won'?'FLIGHT COMPLETE':'SIGNAL ENDED';
  $('result-title').innerHTML=run.status==='won'?'YOU FOUND<br><em>A WAY THROUGH.</em>':'THE RIFT<br><em>WANTS A REMATCH.</em>';
  $('final-score').textContent=String(Math.round(run.score)).padStart(6,'0');$('final-time').textContent=formatTime(run.elapsed);
  $('final-gates').textContent=String(run.gateCount);$('final-kills').textContent=String(run.kills);$('final-combo').textContent=`${run.bestCombo}×`;
  // Record any flight that produced a usable line, not only a win: the ghost is
  // a pace line to beat, and gating it behind a win meant it never appeared.
  lastReplay=run.samples.length>=2?record(run):null;
  const saved=lastReplay?store.save(lastReplay):false;
  $('record-message').textContent=!store.available
    ?'Browser storage is unavailable, so this ghost cannot be kept locally.'
    :run.status==='won'
      ?(saved?'NEW PERSONAL BEST. Your ghost is ready for the next run.':'Flight complete. Your personal-best ghost is waiting for a rematch.')
      :(saved?`BEST ATTEMPT SAVED · ${Math.round(run.distance)} M. Race this ghost on your next run.`
             :'Your best attempt on this course is still further ahead. Race it again.');
  // Only a completed run can be exported: a shared ghost must be a real finish.
  $('save-ghost').disabled=run.status!=='won'||!lastReplay;
  $('save-ghost').title=run.status==='won'?'':'Finish the course to export a shareable ghost.';$('result-dialog').showModal();
}
$('retry').onclick=()=>{$('result-dialog').close();if(lastInputMode==='hands'){toMenu();setupCamera();}else begin('keyboard');};
$('return-menu').onclick=toMenu;$('result-dialog').addEventListener('cancel',e=>{e.preventDefault();toMenu();});
$('save-ghost').onclick=()=>{
  if(!lastReplay)return;
  const blob=new Blob([JSON.stringify(lastReplay)],{type:'application/json'}),url=URL.createObjectURL(blob),a=document.createElement('a');
  a.href=url;a.download=`driftfall-${run.seed}-${run.mode}.ghost.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
};
$('import-ghost').onclick=()=>$('ghost-file').click();
$('ghost-file').onchange=async()=>{
  const file=$('ghost-file').files[0];if(!file)return;
  try{
    if(file.size>MAX_REPLAY_BYTES)throw new Error('Ghost file is too large.');
    importedGhost=parseReplay(await file.text());mode=importedGhost.mode;
    selectMode(mode);$('seed').value=importedGhost.seed;
    showToast(`Ghost loaded: ${formatTime(importedGhost.elapsed)}. Course and mode selected.`);
    $('record-hint').textContent='FRIEND’S GHOST READY · UNVERIFIED REPLAY';
  }catch(error){showToast(`Could not load ghost: ${error.message}`);}finally{$('ghost-file').value='';}
};
function share(){
  const url=new URL(location.href);url.search='';url.hash='';url.searchParams.set('mode',state==='result'?run.mode:mode);url.searchParams.set('seed',state==='result'?run.seed:currentSeed());
  // A custom flight is a different course, so the link has to carry it or the
  // recipient races something else under the same seed.
  const f=cleanFlight(loadout.flight);
  if(!isDefaultFlight(f)){url.searchParams.set('mix',f.mix);url.searchParams.set('density',String(f.density));url.searchParams.set('spread',String(f.spread));}
  $('share-link').value=url.href;$('share-dialog').showModal();
}
$('share-course').onclick=share;$('result-share').onclick=share;$('close-share').onclick=()=>$('share-dialog').close();
$('copy-link').onclick=async()=>{try{await navigator.clipboard.writeText($('share-link').value);$('copy-link').textContent='COPIED ✓';setTimeout(()=>$('copy-link').textContent='COPY LINK ↗',1800);}catch{$('share-link').select();showToast('Select and copy the link above.');}};
$('help').onclick=()=>{helpWasPlaying=['playing','countdown'].includes(state);if(helpWasPlaying)pause();$('help-dialog').showModal();};
$('close-help').onclick=()=>$('help-dialog').close();
$('reduced-motion').checked=reduced;
function applyReduced(){reduced=$('reduced-motion').checked;document.body.classList.toggle('reduced-motion',reduced);}applyReduced();
$('reduced-motion').onchange=applyReduced;
$('show-preview').onchange=()=>$('live-preview').classList.toggle('show-video',$('show-preview').checked);
$('sensitivity').oninput=()=>mapper.sensitivity=Number($('sensitivity').value);
// ---- Flight builder -------------------------------------------------------
function renderPilots(){
  const grid=$('pilot-grid');grid.innerHTML='';
  for(const c of Object.values(CHARACTERS)){
    const b=document.createElement('button');
    b.type='button';b.className='pilot'+(c.id===loadout.character?' selected':'');
    b.setAttribute('role','radio');b.setAttribute('aria-checked',String(c.id===loadout.character));
    b.dataset.pilot=c.id;
    b.innerHTML=`<strong>${c.name}</strong><small>${c.role}</small><span>${c.blurb}</span>`;
    b.onclick=()=>{loadout.character=c.id;renderPilots();updateFlightReadout();};
    grid.append(b);
  }
}
function updateFlightReadout(){
  $('density-out').textContent=`${Number($('flight-density').value).toFixed(1)}×`;
  $('spread-out').textContent=`${Number($('flight-spread').value).toFixed(1)}×`;
  const draft=cleanFlight({mix:$('flight-mix').value,density:$('flight-density').value,spread:$('flight-spread').value});
  const c=course(currentSeed(),mode==='swarm'?'swarm':'race',draft);
  const p=character(loadout.character);
  $('flight-readout').textContent=
    `${c.hostileTotal} hostiles across ${c.waves.length} waves · objective ${c.objectiveTarget} · `+
    `hull ${Math.round(100*p.hull)} · ${isDefaultFlight(draft)?'standard course':'custom course, separate ghosts'}`;
}
function updateLoadoutSummary(){
  const p=character(loadout.character),f=cleanFlight(loadout.flight);
  const mix=f.mix==='balanced'?'BALANCED LATTICE':f.mix==='seekers'?'SEEKERS ONLY':'PYLONS ONLY';
  const extra=f.density!==1||f.spread!==1?` · ${f.density.toFixed(1)}× DENSITY`:'';
  $('loadout-summary').textContent=`${p.name} · ${mix}${extra} · ${loadout.view==='first'?'COCKPIT':'CHASE CAM'}`;
}
function openFlight(){
  $('flight-mix').value=loadout.flight.mix;
  $('flight-density').value=String(loadout.flight.density);
  $('flight-spread').value=String(loadout.flight.spread);
  $('flight-view').value=loadout.view;$('flight-theme').value=loadout.theme;
  renderPilots();updateFlightReadout();$('flight-dialog').showModal();
}
$('build-flight').onclick=openFlight;
$('flight-dialog').addEventListener('cancel',e=>{e.preventDefault();$('flight-dialog').close();});
updateLoadoutSummary();
$('close-flight').onclick=()=>$('flight-dialog').close();
for(const id of ['flight-mix','flight-density','flight-spread'])$(id).oninput=updateFlightReadout;
$('reset-flight').onclick=()=>{
  loadout={character:DEFAULT_CHARACTER,flight:{...DEFAULT_FLIGHT},view:'third',theme:'auto'};
  saveLoadout();openFlight();updateLoadoutSummary();updateRecordHint();
};
$('apply-flight').onclick=()=>{
  loadout.flight=cleanFlight({mix:$('flight-mix').value,density:$('flight-density').value,spread:$('flight-spread').value});
  loadout.view=$('flight-view').value==='first'?'first':'third';
  loadout.theme=$('flight-theme').value;
  saveLoadout();updateLoadoutSummary();updateRecordHint();$('flight-dialog').close();
  showToast(`${character(loadout.character).name} ready · ${isDefaultFlight(loadout.flight)?'standard course':'custom course'}`);
};
$('clear-data').onclick=()=>{store.clear();importedGhost=null;ghost=null;updateRecordHint();showToast('Local ghosts deleted.');};
function syncAudioUI(){
  const a=audio.snapshot();
  $('sound').setAttribute('aria-pressed',String(a.enabled));
  $('sound').setAttribute('aria-label',a.enabled?'Mute music and sound effects':'Enable music and sound effects');
  $('sound').innerHTML=`SOUND ${a.enabled?'ON':'OFF'} <span aria-hidden="true">♪</span>`;
  $('audio-enable').setAttribute('aria-pressed',String(a.enabled));
  $('audio-enable').textContent=a.enabled?'MUTE ALL SOUND  ×':'TURN ON SOUND  ♪';
  for(const key of ['music','effects']){
    $(key+'-volume').value=String(Math.round(a[key]*100));
    $(key+'-level').textContent=`${Math.round(a[key]*100)}%`;
  }
  $('music-status').textContent=a.error||(!a.enabled?'PRESS TURN ON SOUND TO LISTEN':a.music===0?'MUSIC MUTED · EFFECTS ARE SEPARATE':a.suspended?'FLIGHT PAUSED · SOUND HELD':'NEON WAKE · ORIGINAL SYNTHWAVE · 116 BPM');
}
async function toggleSound(){
  await audio.toggle();syncAudioUI();
  if(audio.error)showToast(audio.error);
}
$('sound').onclick=toggleSound;$('audio-enable').onclick=toggleSound;
$('audio-settings').onclick=()=>{
  if(['playing','countdown'].includes(state))pause('Flight held while you adjust the sound.');
  // The mixer previews the quieter hangar arrangement. Closing it leaves an
  // interrupted flight paused; it never resumes movement or the webcam.
  audio.setScene('menu');audio.resume().then(syncAudioUI);syncAudioUI();$('audio-dialog').showModal();
};
function closeAudioSettings(){
  $('audio-dialog').close();audio.setScene(state);
  if(state==='paused'||document.hidden)audio.suspend();else audio.resume();
  syncAudioUI();
}
$('close-audio').onclick=closeAudioSettings;
$('audio-dialog').addEventListener('cancel',e=>{e.preventDefault();closeAudioSettings();});
for(const key of ['music','effects'])$(key+'-volume').oninput=()=>{audio.setVolume(key,Number($(key+'-volume').value)/100);syncAudioUI();};
syncAudioUI();
window.addEventListener('keydown',e=>{
  if(['INPUT','SELECT','TEXTAREA'].includes(e.target.tagName))return;
  if(e.code==='KeyM'&&!e.repeat){e.preventDefault();toggleSound();return;}
  if(e.code==='F3'){e.preventDefault();$('diagnostics').hidden=!$('diagnostics').hidden;return;}
  if(e.code==='KeyP'&&state==='paused'&&$('pause-dialog').open&&!$('help-dialog').open){e.preventDefault();resume();return;}
  if(document.querySelector('dialog[open]'))return;
  if((e.code==='Escape'||e.code==='KeyP')&&['playing','countdown'].includes(state)){e.preventDefault();pause();return;}
  if(!['playing','countdown'].includes(state))return;
  if(['Space','ArrowUp','ArrowDown','ArrowLeft','ArrowRight','ShiftLeft','ShiftRight'].includes(e.code))e.preventDefault();
  keys.add(e.code);
  if(e.code==='KeyR'&&inputMode==='hands')showToast(mapper.calibrate(performance.now())?'Hands recentered.':'Hold the steering hand steady, then press R.');
});
window.addEventListener('keyup',e=>keys.delete(e.code));
canvas.addEventListener('pointerdown',e=>{
  if(state!=='playing')return;e.preventDefault();canvas.setPointerCapture(e.pointerId);
  if(e.pointerType==='touch'){pointer.touch=true;pointer.tx=(e.clientX/innerWidth-.5)*2;pointer.ty=(.5-e.clientY/innerHeight)*2;}
  else{pointer.down=e.button===0;pointer.aim=true;const p=renderer.pointerAim(e.clientX,e.clientY);pointer.x=p.x;pointer.y=p.y;}
});
canvas.addEventListener('pointermove',e=>{
  if(state!=='playing')return;
  if(e.pointerType==='touch'&&pointer.touch){pointer.tx=(e.clientX/innerWidth-.5)*2;pointer.ty=(.5-e.clientY/innerHeight)*2;}
  else if(e.pointerType!=='touch'){pointer.aim=true;const p=renderer.pointerAim(e.clientX,e.clientY);pointer.x=p.x;pointer.y=p.y;}
});
for(const event of ['pointerup','pointercancel','lostpointercapture'])canvas.addEventListener(event,()=>{pointer.down=false;pointer.touch=false;});
canvas.addEventListener('contextmenu',e=>e.preventDefault());
for(const[id,key]of[['touch-fire','fire'],['touch-boost','boost']]){
  $(id).addEventListener('pointerdown',e=>{e.preventDefault();$(id).setPointerCapture(e.pointerId);pointer[key]=true;$(id).classList.add('active');});
  for(const event of ['pointerup','pointercancel','lostpointercapture'])$(id).addEventListener(event,()=>{pointer[key]=false;$(id).classList.remove('active');});
}
window.addEventListener('blur',()=>{clearInput();if(['playing','countdown'].includes(state))pause('Flight paused because the window lost focus.');});
document.addEventListener('visibilitychange',()=>{
  if(document.hidden){
    if(['playing','countdown'].includes(state))pause('Flight paused while the tab is hidden. Camera access has been stopped.');
    camera.stop();clearInput();audio.suspend();
    if($('camera-dialog').open){$('camera-error').textContent='Setup stopped when this tab became hidden. Close and reopen setup to give fresh consent.';}
    if(state==='paused'){$('pause-stop-camera').hidden=true;$('pause-keyboard').hidden=inputMode!=='hands';}
  }else{lastTime=performance.now();accumulator=0;if(['menu','result'].includes(state))audio.resume().then(syncAudioUI);}
});
window.addEventListener('pagehide',()=>{camera.stop();audio.suspend();clearInput();});
window.addEventListener('pageshow',e=>{if(e.persisted&&!document.hidden&&['menu','result'].includes(state))audio.resume().then(syncAudioUI);});
canvas.addEventListener('webglcontextlost',e=>{e.preventDefault();camera.stop();pause('The graphics context was lost. Reload the page to restore it.');$('fatal').hidden=false;$('fatal-message').textContent='Graphics context lost. Your camera was turned off. Reload to restore the flight deck.';});
function input(now){
  if(inputMode==='hands'){
    const h=mapper.input(now);
    // Keyboard and touch stay live as a backup for BOTH fire and boost: pinch
    // detection can struggle, and a player with no way to shoot has no game.
    return{...h,
      fire:h.fire||keys.has('Space')||pointer.down||pointer.fire,
      boost:h.boost||keys.has('ShiftLeft')||keys.has('ShiftRight')||pointer.boost};
  }
  const x=Number(keys.has('KeyD')||keys.has('ArrowRight'))-Number(keys.has('KeyA')||keys.has('ArrowLeft'));
  const y=Number(keys.has('KeyW')||keys.has('ArrowUp'))-Number(keys.has('KeyS')||keys.has('ArrowDown'));
  return{x:pointer.touch?clamp(pointer.tx,-1,1):x,y:pointer.touch?clamp(pointer.ty,-1,1):y,target:pointer.touch,
    fire:keys.has('Space')||pointer.down||pointer.fire,boost:keys.has('ShiftLeft')||keys.has('ShiftRight')||pointer.boost,
    aimX:pointer.x,aimY:pointer.y,aimActive:pointer.aim&&!pointer.touch};
}
function updateHud(now){
  $('score').textContent=String(Math.round(run.score)).padStart(6,'0');$('combo').textContent=`×${Math.max(1,run.combo)}`;
  $('timer').textContent=formatTime(mode==='swarm'?Math.max(0,90-run.elapsed):run.elapsed);
  $('speed').textContent=String(Math.round(run.speed*7.2)).padStart(3,'0');$('health-number').textContent=String(Math.round(run.health));
  $('health-fill').style.width=`${run.maxHealth?run.health/run.maxHealth*100:0}%`;$('boost-fill').style.width=`${run.energy}%`;$('heat-fill').style.width=`${run.heat*100}%`;
  $('heat-label').textContent=run.overheated?'COOLING':'READY';
  $('gate-tally').textContent=`${run.gateCount} GATES`;$('kill-tally').textContent=`${run.kills} LATTICE DOWN`;
  const objTarget=run.objectiveTarget||0,objDone=Math.min(run.kills,objTarget);
  $('objective-text').textContent=run.objectiveDone?`OBJECTIVE CLEAR · ${objTarget} DOWN`:`DOWN THE LATTICE ${objDone} / ${objTarget}`;
  $('objective-fill').style.width=`${objTarget?objDone/objTarget*100:0}%`;
  $('objective').classList.toggle('complete',Boolean(run.objectiveDone));
  $('progress-fill').style.width=`${clamp(mode==='swarm'?run.elapsed/90:run.distance/run.length,0,1)*100}%`;
  // A pinned theme holds one biome for the whole run, so the label must follow
  // the same choice the renderer made rather than the distance.
  const pinnedBiome=BIOMES.find(b=>b.id===loadout.theme);
  $('sector-name').textContent=(pinnedBiome||BIOMES[Math.min(2,Math.floor(run.distance/700))]).name;
  const g=ghostAt(ghost,run.elapsed);$('ghost-gap').textContent=g?`${Math.abs(Math.round(run.distance-g.distance))} M ${run.distance>=g.distance?'AHEAD OF':'BEHIND'} GHOST`:'SOLO FLIGHT';
  if(now>calloutUntil){$('callout').textContent='';$('callout').classList.remove('warn','good');}
  if(!$('diagnostics').hidden)$('diagnostics').textContent=JSON.stringify({build:'0.1.1-music',...renderer?.stats,input:inputMode,handInferenceMs:+inferenceMs.toFixed(1),camera:camera.active?'on':'off',simulationHz:120,entities:run.entities.length,shots:run.shots},null,2);
}
function frame(now){
  if(!renderer)return;
  requestAnimationFrame(frame);
  const rawDt=lastTime?(now-lastTime)/1000:0;lastTime=now;
  if(document.hidden)return;
  const dt=clamp(rawDt,0,.1);
  if(['playing','countdown'].includes(state)&&rawDt>.6)pause('A long browser stall was detected. Flight paused to keep you safe.');
  if(inputMode==='hands'&&['playing','countdown'].includes(state)&&mapper.lost(now))pause('The steering hand left view. Bring it back, then resume. No shots are being fired.',true);
  if(state==='countdown'){
    countdown-=dt;$('countdown').textContent=countdown>.6?String(Math.ceil(countdown)):'GO';
    if(countdown<=0){setState('playing');audio.tone(740,.18,.035,'sine',990);}
  }else if(state==='playing'){
    activeInput=input(now);accumulator+=dt;
    let steps=0;while(accumulator>=STEP&&steps<12&&run.status==='running'){run.step(activeInput,STEP);accumulator-=STEP;steps++;}
    audio.setScene('playing',run.boosting?1:Math.min(.65,run.distance/run.length*.6));
    const events=run.drainEvents();renderer.effects(events,run);audio.events(events);
    for(const e of events){
      if(e.type==='gate'){$('callout').textContent=e.perfect?`PERFECT LINE · ×${e.combo}`:`GATE CHAIN · ×${e.combo}`;calloutUntil=now+1000;}
      if(e.type==='overheat'){$('callout').textContent='COOLING DOWN';calloutUntil=now+1100;}
      if(e.type==='damage'){
        $('callout').textContent=`✖ HULL HIT · -${e.amount} · ${Math.max(0,Math.round(run.health))}% HULL`;
        $('callout').classList.add('warn');calloutUntil=now+900;
      }
      if(e.type==='wave'){
        const parts=[];
        if(e.seekers)parts.push(`${e.seekers} SEEKER${e.seekers>1?'S':''}`);
        if(e.pylons)parts.push(`${e.pylons} PYLON${e.pylons>1?'S':''}`);
        // The first wave also teaches the fire control, in context.
        const teach=e.index===0?(inputMode==='hands'?' · PINCH TO FIRE':' · SPACE TO FIRE'):'';
        $('callout').textContent=`⚠ WAVE ${e.index+1}/${e.total} · ${parts.join(' + ')} INBOUND${teach}`;
        $('callout').classList.add('warn');calloutUntil=now+(e.index===0?2400:1500);
        audio.tone(196,.16,.03,'sawtooth',150);
      }
      if(e.type==='objective'){
        $('callout').textContent=`OBJECTIVE CLEAR · ${e.target} DOWN · +1000`;
        $('callout').classList.add('good');calloutUntil=now+2000;
        audio.tone(660,.22,.04,'triangle',990);
      }
    }
    if(run.status!=='running')finish();
  }
  // Keep capture responsive while a modal covers the scene. Drawing through
  // a software-composited backdrop can otherwise starve calibration.
  const modalOpen=Boolean(document.querySelector('dialog[open]'));
  if(!modalOpen||now-lastSceneDraw>400){
    renderer.draw(run,now/1000,{menu:state==='menu',reducedMotion:reduced,ghost,dt:state==='playing'?dt:0,
      view:loadout.view,theme:loadout.theme});
    lastSceneDraw=now;
  }
  if(state!=='menu'){
    const target=renderer.screenPoint(activeInput.aimActive?activeInput.aimX:run.player.x,activeInput.aimActive?activeInput.aimY:run.player.y);
    $('target').style.left=`${target.x}px`;$('target').style.top=`${target.y}px`;$('target').classList.toggle('firing',Boolean(activeInput.fire));
    const next=run.gates.find(g=>!g.passed);if(next){const p=renderer.screenPoint(next.x,next.y,run.distance-next.z);$('next-gate').hidden=!p.visible;$('next-gate').style.left=`${clamp(p.x,30,innerWidth-30)}px`;$('next-gate').style.top=`${clamp(p.y,170,innerHeight-100)}px`;}
  }
  if(now-lastHud>80){updateHud(now);lastHud=now;}
}
// Read-only observability, deliberately no score setter or camera-data access.
Object.defineProperty(window,'__DRIFTFALL__',{value:Object.freeze({snapshot:()=>({...run.snapshot(),ui:state,inputMode,camera:{active:camera.active,ready:camera.ready},render:{...renderer?.stats},audio:audio.snapshot()})}),writable:false});
if(renderer)requestAnimationFrame(frame);
