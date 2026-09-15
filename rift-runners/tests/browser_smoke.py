"""Browser journeys. Normal mode serves shipped ES modules over HTTP.
--memory is for restricted environments: load the same sources as blob modules,
with CSS injected and the document CSP removed only in the test document.
The latter is NOT a test of deployed CSP, remote model downloads, or HTTP loading.
Camera tests use a synthetic capture/worker, never a real webcam.
"""
from pathlib import Path
import argparse, json, re, time, traceback
from playwright.sync_api import sync_playwright
ROOT=Path(__file__).resolve().parents[1]
ORDER=['core.js','math.js','replay.js','gestures.js','camera.js','music.js','audio.js','renderer.js','app.js']
parser=argparse.ArgumentParser()
parser.add_argument('--base-url',default='http://127.0.0.1:4173/driftfall/')
parser.add_argument('--memory',action='store_true')
parser.add_argument('--compatibility',action='store_true')
parser.add_argument('--executable')
parser.add_argument('--out',default='qa-output')
parser.add_argument('--journey-only',action='store_true')
parser.add_argument('--audio-only',action='store_true')
args=parser.parse_args();OUT=Path(args.out);OUT.mkdir(parents=True,exist_ok=True)
results=[]
MOCK=r'''() => {
 window.__cameraMock={calls:0,stops:0,terminated:0,frames:0,kind:'deny',hands:true,x:.5,pinch:1};
 const m=window.__cameraMock;
 Object.defineProperty(window,'isSecureContext',{configurable:true,value:true});
 Object.defineProperty(navigator,'mediaDevices',{configurable:true,value:{getUserMedia:async constraints=>{
  m.calls++;m.constraints=constraints;
  if(m.kind==='deny')throw new DOMException('User denied camera permission','NotAllowedError');
  const stream={getTracks:()=>[{stop:()=>m.stops++}],getVideoTracks:()=>[{addEventListener:()=>{}}]};
  if(m.kind==='pending')return new Promise(resolve=>m.resolve=()=>resolve(stream));
  return stream;
 }}});
 Object.defineProperty(HTMLMediaElement.prototype,'srcObject',{configurable:true,get(){return this.__stream||null},set(value){this.__stream=value}});
 Object.defineProperty(HTMLMediaElement.prototype,'readyState',{configurable:true,get:()=>4});
 Object.defineProperty(HTMLMediaElement.prototype,'currentTime',{configurable:true,get:()=>performance.now()/1000});
 HTMLMediaElement.prototype.play=async()=>{};HTMLMediaElement.prototype.pause=()=>{};
 window.createImageBitmap=async()=>({close:()=>{}});
 window.Worker=class{
  terminate(){m.terminated++;this.closed=true}
  postMessage(data){
   if(this.closed)return;
   if(data.type==='init'){queueMicrotask(()=>this.onmessage?.({data:{type:'ready'}}));return;}
   if(data.type==='frame'){
    m.frames++;const x=m.x,y=.5;const pts=Array.from({length:21},()=>({x,y,z:0}));
    pts[0]={x,y:y+.1};pts[5]={x:x-.05,y:y+.02};pts[9]={x,y};pts[17]={x:x+.05,y:y+.02};
    pts[4]={x:x-.03,y:y-.08};pts[8]={x:x-.03+m.pinch*.1,y:y-.08};
    queueMicrotask(()=>this.onmessage?.({data:{type:'result',landmarks:m.hands?[pts]:[],handedness:[[{categoryName:'Right',score:.99}]],timestamp:data.timestamp,inferenceMs:3}}));
   }
  }
 };
}'''
FAST=r'''() => {
 const native=requestAnimationFrame.bind(window),origin=performance.now();
 window.requestAnimationFrame=callback=>native(t=>callback(origin+(t-origin)*3));
}'''
def load(page,before=None,width=1280,height=800):
    page.set_viewport_size({'width':width,'height':height})
    if args.memory:
        html=(ROOT/'index.html').read_text()
        html=re.sub(r'<meta http-equiv="Content-Security-Policy"[^>]*>','',html)
        html=re.sub(r'<link[^>]+>','',html)
        html=re.sub(r'<script type="module"[\s\S]*?</script>','',html)
        page.set_content(html)
        page.add_style_tag(content=(ROOT/'styles.css').read_text())
        if before: page.evaluate(before)
        sources=[{'name':name,'code':(ROOT/'src'/name).read_text()} for name in ORDER]
        page.evaluate(r'''async sources=>{
         const urls={};
         for(const{name,code}of sources){
          let text=code.replace(/from ['"]\.\/([^'"]+)['"]/g,(_,file)=>`from '${urls[file]}'`);
          text=text.replace("new URL('./vision-worker.js', import.meta.url)","new URL('https://driftfall.test/src/vision-worker.js')");
          urls[name]=URL.createObjectURL(new Blob([text],{type:'text/javascript'}));
         }
         window.__TEST_MODULES__=urls;await import(urls['app.js']);
        }''',sources)
    else:
        if before: page.add_init_script('('+before+')()')
        page.goto(args.base_url,wait_until='networkidle')
    wait_for(page,'!!window.__DRIFTFALL__')
    assert not page.locator('#fatal').is_visible(),page.locator('#fatal-message').inner_text()
    page.wait_for_timeout(150)

def wait_for(page, expression, timeout=30000):
    # Poll from the test driver. Playwright's in-page string predicate can use
    # eval asynchronously and conflict with the shipped no-unsafe-eval CSP.
    # This does not modify the page policy or application execution environment.
    deadline=time.monotonic()+timeout/1000
    while time.monotonic()<deadline:
        if page.evaluate(expression):return
        page.wait_for_timeout(50)
    raise AssertionError('Timed out waiting for: '+expression)

def snapshot(page):return page.evaluate('window.__DRIFTFALL__.snapshot()')
def start(page):
    page.locator('#play-keyboard').click()
    wait_for(page,"window.__DRIFTFALL__.snapshot().ui==='playing'",timeout=10000)

def execute(browser,name,fn,touch=False):
    context=browser.new_context(has_touch=touch,accept_downloads=True)
    page=context.new_page();errors=[]
    page.on('pageerror',lambda e:errors.append(str(e)))
    began=time.monotonic()
    try:
        fn(page,context)
        assert not errors,errors
        results.append({'test':name,'status':'pass','seconds':round(time.monotonic()-began,2),'renderer':snapshot(page)['render']['backend']})
        print('PASS',name,flush=True)
    except Exception as e:
        results.append({'test':name,'status':'fail','error':str(e),'trace':traceback.format_exc()})
        print('FAIL',name,str(e),flush=True)
        try:page.screenshot(path=str(OUT/(re.sub(r'[^a-z0-9]+','-',name.lower())+'-failure.png')))
        except Exception:pass
    finally:context.close()

def landing(page,context):
    requests=[];page.on('request',lambda req:requests.append(req.url))
    load(page,MOCK,width=1440,height=900)
    assert page.evaluate('__cameraMock.calls')==0
    assert snapshot(page)['camera']['active'] is False
    expected='Canvas2D compatibility' if args.compatibility else 'WebGL2'
    assert snapshot(page)['render']['backend']==expected, 'Expected '+expected+'; inspect browser graphics support.'
    assert not any('jsdelivr' in u or 'googleapis' in u for u in requests)
    page.screenshot(path=str(OUT/'landing-desktop.png'))

def keyboard(page,context):
    load(page,MOCK);start(page)
    page.keyboard.down('KeyD');page.keyboard.down('Space');page.keyboard.down('Shift')
    page.wait_for_timeout(650)
    page.keyboard.up('KeyD');page.keyboard.up('Space');page.keyboard.up('Shift')
    s=snapshot(page);assert s['player']['x']>2 and s['shots']>0 and s['energy']<100,s
    assert page.evaluate('__cameraMock.calls')==0
    page.screenshot(path=str(OUT/'flight-desktop.png'))
    page.keyboard.press('KeyP');assert snapshot(page)['ui']=='paused'
    frozen=snapshot(page)['elapsed'];page.wait_for_timeout(250);assert snapshot(page)['elapsed']==frozen
    page.locator('#resume').click();page.wait_for_timeout(150);assert snapshot(page)['ui']=='playing'
    page.keyboard.press('KeyP');page.locator('#quit').click();assert snapshot(page)['ui']=='menu'
    page.locator('#play-keyboard').click();assert snapshot(page)['elapsed']==0 and snapshot(page)['score']==0

def consent_denied(page,context):
    load(page,MOCK);page.locator('#play-hands').click()
    assert page.locator('#allow-camera').is_disabled();assert page.evaluate('__cameraMock.calls')==0
    page.screenshot(path=str(OUT/'camera-consent.png'))
    page.locator('#camera-consent').check();page.locator('#allow-camera').click()
    wait_for(page,'__cameraMock.calls===1')
    assert page.evaluate('__cameraMock.constraints.audio') is False
    wait_for(page,"document.querySelector('#camera-error').textContent.includes('denied')")
    assert not snapshot(page)['camera']['active']
    page.locator('#calibration-fallback').click();assert snapshot(page)['ui']=='countdown'

def late_cancel(page,context):
    load(page,MOCK);page.evaluate("__cameraMock.kind='pending'")
    page.locator('#play-hands').click();page.locator('#camera-consent').check();page.locator('#allow-camera').click()
    wait_for(page,'__cameraMock.calls===1');page.locator('#cancel-camera').click()
    page.evaluate('__cameraMock.resolve()');page.wait_for_timeout(100)
    assert page.evaluate('__cameraMock.stops')==1
    assert not snapshot(page)['camera']['active']
    page.locator('#play-hands').click();assert not page.locator('#camera-consent').is_checked()
    assert page.locator('#allow-camera').is_disabled()

def mock_hand_start(page):
    load(page,MOCK);page.evaluate("__cameraMock.kind='success'")
    page.locator('#play-hands').click();page.locator('#camera-consent').check();page.locator('#allow-camera').click()
    wait_for(page,"!document.querySelector('#calibrate').disabled",timeout=5000)
    page.locator('#calibrate').click();wait_for(page,"window.__DRIFTFALL__.snapshot().ui==='playing'",timeout=10000)

def hands(page,context):
    mock_hand_start(page)
    page.evaluate('__cameraMock.x=.42;__cameraMock.pinch=.2')
    page.wait_for_timeout(600);s=snapshot(page)
    assert s['player']['x']>1 and s['shots']>0,s
    page.screenshot(path=str(OUT/'synthetic-hand-control.png'))
    page.evaluate('__cameraMock.hands=false')
    wait_for(page,"window.__DRIFTFALL__.snapshot().ui==='paused'",timeout=3000)
    shots=snapshot(page)['shots'];page.wait_for_timeout(200);assert snapshot(page)['shots']==shots
    page.locator('#pause-stop-camera').click()
    assert page.evaluate('__cameraMock.stops')==1 and page.evaluate('__cameraMock.terminated')==1
    assert not snapshot(page)['camera']['active']
    page.locator('#resume').click();assert snapshot(page)['inputMode']=='keyboard'

def visibility(page,context):
    mock_hand_start(page)
    page.evaluate("Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'))")
    assert snapshot(page)['ui']=='paused'
    assert page.evaluate('__cameraMock.stops')==1
    assert not snapshot(page)['camera']['active']

def mobile(page,context):
    load(page,MOCK,width=390,height=844)
    for id in ['play-hands','play-keyboard','share-course']:
        bb=page.locator('#'+id).bounding_box();assert bb and bb['x']>=0 and bb['x']+bb['width']<=391 and bb['y']+bb['height']<=844,(id,bb)
    page.screenshot(path=str(OUT/'landing-mobile.png'))
    start(page)
    cdp=context.new_cdp_session(page)
    cdp.send('Input.dispatchTouchEvent',{'type':'touchStart','touchPoints':[{'x':290,'y':400}]})
    page.wait_for_timeout(400)
    cdp.send('Input.dispatchTouchEvent',{'type':'touchEnd','touchPoints':[]})
    assert snapshot(page)['player']['x']>1
    bb=page.locator('#touch-fire').bounding_box()
    cdp.send('Input.dispatchTouchEvent',{'type':'touchStart','touchPoints':[{'x':bb['x']+bb['width']/2,'y':bb['y']+bb['height']/2}]})
    page.wait_for_timeout(450)
    cdp.send('Input.dispatchTouchEvent',{'type':'touchEnd','touchPoints':[]})
    assert snapshot(page)['shots']>0
    page.screenshot(path=str(OUT/'flight-mobile.png'))

def invalid_replay(page,context):
    load(page)
    page.locator('#ghost-file').set_input_files({'name':'bad.ghost.json','mimeType':'application/json','buffer':b'{"format":"not-a-ghost"}'})
    wait_for(page,"document.querySelector('#toast').textContent.includes('Could not load')")
    assert snapshot(page)['ui']=='menu'

def settings(page,context):
    load(page)
    page.locator('[data-mode="swarm"]').click();assert page.locator('[data-mode="swarm"]').get_attribute('aria-pressed')=='true'
    page.locator('[data-mode="daily"]').click();assert page.locator('#seed').input_value().startswith('DAILY-')
    assert page.locator('#seed').get_attribute('readonly') is not None
    page.locator('#help').click();page.locator('#reduced-motion').check();assert page.locator('body').get_attribute('class')=='reduced-motion'
    page.locator('#close-help').click()
    page.locator('#share-course').click();assert 'mode=daily' in page.locator('#share-link').input_value()

def full_journey(page,context):
    # Clock acceleration is explicit. Actual key events drive the real loop;
    # no health, score, distance or terminal-state setter is used.
    load(page,FAST,width=960,height=640);start(page)
    held=set();page.keyboard.down('Space');page.keyboard.down('Shift')
    deadline=time.monotonic()+48
    while time.monotonic()<deadline:
        s=snapshot(page)
        if s['ui']=='result':break
        assert s['ui']=='playing',s
        g=s['nextGate'];wanted=set()
        if g:
            dx=g['x']-s['player']['x'];dy=g['y']-s['player']['y']
            if abs(dx)>.20:wanted.add('KeyD' if dx>0 else 'KeyA')
            if abs(dy)>.16:wanted.add('KeyW' if dy>0 else 'KeyS')
        for key in held-wanted:page.keyboard.up(key)
        for key in wanted-held:page.keyboard.down(key)
        held=wanted;page.wait_for_timeout(35)
    for key in held|{'Space','Shift'}:page.keyboard.up(key)
    s=snapshot(page);assert s['ui']=='result',s
    # This gate-following bot does not avoid obstacles. A genuine loss is a
    # valid terminal journey, not evidence that the course is unbeatable.
    assert s['status'] in ('won','lost'),s
    assert page.locator('#save-ghost').is_enabled()==(s['status']=='won')
    page.screenshot(path=str(OUT/'terminal-flight.png'))
    (OUT/'terminal-flight-state.json').write_text(json.dumps(s,indent=2))
    page.locator('#retry').click();assert snapshot(page)['elapsed']==0 and snapshot(page)['score']==0


# These use the browser's REAL AudioContext (not the camera/inference mock).
# The wrapper only observes construction and taps an output gain for measurement.
AUDIO_OBSERVER=r"""() => {
 window.__audioContexts=[];
 const Native=window.AudioContext||window.webkitAudioContext;
 if(!Native)return;
 window.AudioContext=class extends Native{
  constructor(options){super(options);window.__audioContexts.push(this);}
  createGain(){const n=super.createGain();this.__gains??=[];this.__gains.push(n);return n;}
 };
}"""
def audio_load(page,width=1280,height=800):
    before='() => { ('+MOCK+')(); ('+AUDIO_OBSERVER+')(); }'
    load(page,before,width=width,height=height)

def audio_rms(page):
    return page.evaluate(r"""() => {
     const ctx=window.__audioContexts[0];
     if(!ctx||ctx.state!=='running')return 0;
     if(!ctx.__meter){ctx.__meter=ctx.createAnalyser();ctx.__meter.fftSize=2048;ctx.__gains[0].connect(ctx.__meter);}
     const data=new Float32Array(2048);ctx.__meter.getFloatTimeDomainData(data);
     return Math.sqrt(data.reduce((sum,x)=>sum+x*x,0)/data.length);
    }""")

def audio_opt_in(page,context):
    requests=[];page.on('request',lambda req:requests.append(req.url))
    audio_load(page)
    assert snapshot(page)['audio']['context']=='not-created'
    assert page.evaluate('__audioContexts.length')==0
    page.locator('#sound').click()
    wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='running'")
    audio_rms(page);page.wait_for_timeout(400)
    readings=[]
    for _ in range(6):readings.append(audio_rms(page));page.wait_for_timeout(60)
    assert max(readings)>0.0001,readings
    assert snapshot(page)['audio']['scheduledSteps']>1
    assert page.evaluate('__cameraMock.calls')==0
    assert not any('jsdelivr' in u or 'googleapis' in u for u in requests),requests
    page.locator('#sound').click()
    wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='suspended'")
    assert snapshot(page)['audio']['activeVoices']==0
    assert not snapshot(page)['audio']['schedulerActive']
    (OUT/'live-audio-levels.json').write_text(json.dumps({'rms':readings,'real_audio_context':True,'physical_speakers_tested':False},indent=2))

def audio_flight(page,context):
    audio_load(page);page.locator('#sound').click();start(page)
    assert snapshot(page)['audio']['scene']=='flight'
    page.keyboard.down('Shift');page.wait_for_timeout(200)
    assert snapshot(page)['audio']['intensity']==1
    page.keyboard.up('Shift');page.keyboard.press('KeyP')
    wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='suspended'")
    assert snapshot(page)['audio']['enabled']
    assert not snapshot(page)['audio']['schedulerActive']
    page.locator('#quit').click()
    wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='running'")
    assert snapshot(page)['audio']['scene']=='hangar'
    page.keyboard.press('KeyM')
    wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='suspended'")
    assert not snapshot(page)['audio']['enabled']

def audio_mixer(page,context):
    audio_load(page,width=390,height=844)
    page.locator('#audio-settings').click()
    assert snapshot(page)['audio']['context']=='not-created'
    page.locator('#audio-enable').click()
    wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='running'")
    page.locator('#music-volume').fill('23');page.locator('#music-volume').dispatch_event('input')
    assert snapshot(page)['audio']['music']==.23
    assert snapshot(page)['audio']['effects']==.65
    page.locator('#effects-volume').fill('0');page.locator('#effects-volume').dispatch_event('input')
    assert snapshot(page)['audio']['effects']==0
    assert page.locator('#music-level').inner_text()=='23%'
    assert page.locator('#effects-level').inner_text()=='0%'
    assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
    page.screenshot(path=str(OUT/'music-mixer-mobile.png'))
    page.locator('#close-audio').click()
    start(page);page.locator('#audio-settings').click()
    assert snapshot(page)['ui']=='paused'
    page.locator('#close-audio').click()
    assert snapshot(page)['ui']=='paused' # Never resume the ship automatically.
    wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='suspended'")
    page.locator('#resume').click();wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='running'")

def audio_hidden(page,context):
    audio_load(page);page.locator('#sound').click()
    wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='running'")
    page.evaluate("Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'))")
    wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='suspended'")
    assert snapshot(page)['audio']['activeVoices']==0
    assert not snapshot(page)['audio']['schedulerActive']
    page.evaluate("Object.defineProperty(document,'hidden',{configurable:true,value:false});document.dispatchEvent(new Event('visibilitychange'))")
    wait_for(page,"window.__DRIFTFALL__.snapshot().audio.context==='running'")
    assert snapshot(page)['audio']['schedulerActive']

AUDIO_CASES=[
 ('Real Web Audio opt-in, non-silent output, mute, no camera/network requests',audio_opt_in,False),
 ('Flight/boost music state, pause silence, menu recovery and M mute',audio_flight,False),
 ('Mobile mixer, independent levels, and safe interrupted-flight behavior',audio_mixer,True),
 ('Hidden-tab audio shutdown and visible-menu recovery',audio_hidden,False)
]

with sync_playwright() as p:
    flags=['--no-sandbox','--enable-unsafe-swiftshader','--use-gl=angle','--use-angle=swiftshader','--disable-dev-shm-usage']
    if args.compatibility:flags+=['--disable-webgl']
    launch={'headless':True,'args':flags}
    if args.executable:launch['executable_path']=args.executable
    browser=p.chromium.launch(**launch)
    try:
        if args.audio_only:
            for name,fn,touch in AUDIO_CASES:execute(browser,name,fn,touch)
        elif args.journey_only:execute(browser,'Generated race reaches real win/loss, then clean restart (3x test clock)',full_journey)
        else:
            for name,fn,touch in [
                ('Landing, no camera calls or model requests',landing,False),
                ('Keyboard steering, fire, boost, pause, resume and clean restart',keyboard,False),
                ('Consent gate, explicit denial and usable fallback',consent_denied,False),
                ('Late permission resolution after cancel and fresh consent',late_cancel,False),
                ('Synthetic hands steer and fire; loss pauses; stop releases camera',hands,False),
                ('Tab visibility shuts down synthetic camera',visibility,False),
                ('Mobile layout and real touch steering and firing',mobile,True),
                ('Malformed replay is rejected without leaving menu',invalid_replay,False),
                ('Mode, daily course, reduced motion and sharing UI',settings,False)
            ]+AUDIO_CASES:execute(browser,name,fn,touch)
    finally:browser.close()
report={'environment':{'memory_harness':args.memory,'compatibility_requested':args.compatibility,'executable':args.executable or 'Playwright Chromium'},'camera':'synthetic only; no live model validation','audio':'real Web Audio graph; no physical speaker/device listening test','tests':results}
(OUT/('journey-report.json' if args.journey_only else 'audio-report.json' if args.audio_only else 'browser-report.json')).write_text(json.dumps(report,indent=2))
raise SystemExit(any(r['status']=='fail' for r in results))
