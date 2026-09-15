"""Render the exact game score/instruments with Chromium OfflineAudioContext.
Developer-only: requires Playwright/Chromium; no external music downloads.
An offline render pre-schedules the entire score, so its queue cap is enlarged;
the in-game live voice/scheduling limits are unchanged.
"""
from pathlib import Path
import argparse,base64,json
from playwright.sync_api import sync_playwright
parser=argparse.ArgumentParser()
parser.add_argument('--out',default='qa-music-preview')
parser.add_argument('--executable')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1];out=Path(args.out);out.mkdir(parents=True,exist_ok=True)
source=(root/'src/music.js').read_text()
with sync_playwright() as p:
    options={'headless':True,'args':['--no-sandbox','--disable-dev-shm-usage']}
    if args.executable:options['executable_path']=args.executable
    browser=p.chromium.launch(**options)
    page=browser.new_page();page.set_content('<html><body>Offline audio renderer</body></html>')
    result=page.evaluate(r'''async source=>{
      const url=URL.createObjectURL(new Blob([source],{type:'text/javascript'}));
      const {MUSIC,MusicSynth,scoreStep}=await import(url);
      const sampleRate=44100,steps=288,duration=steps*MUSIC.stepSeconds+.25;
      const context=new OfflineAudioContext(2,Math.ceil(duration*sampleRate),sampleRate);
      const limiter=context.createDynamicsCompressor();limiter.threshold.value=-12;limiter.knee.value=10;limiter.ratio.value=8;limiter.attack.value=.003;limiter.release.value=.2;
      const music=context.createGain();music.gain.value=.48;
      const master=context.createGain();master.gain.setValueAtTime(0,0);master.gain.linearRampToValueAtTime(.82,.4);master.gain.setValueAtTime(.82,duration-2);master.gain.linearRampToValueAtTime(0,duration-.01);
      music.connect(limiter);limiter.connect(master);master.connect(context.destination);
      const synth=new MusicSynth(context,music);synth.maxVoices=12000;
      let count=0;
      for(let i=0;i<steps;i++){
       const scene=i<64||i>=256?'hangar':'flight',intensity=i>=192&&i<256?1:.35;
       for(const event of scoreStep(i,{scene,intensity})){synth.play(event,i*MUSIC.stepSeconds+.1);count++;}
      }
      const buffer=await context.startRendering(),left=buffer.getChannelData(0),right=buffer.getChannelData(1);
      const rms=(start,end)=>{let sum=0,n=0;for(let i=Math.floor(start*sampleRate);i<Math.min(left.length,Math.floor(end*sampleRate));i++){sum+=(left[i]**2+right[i]**2)/2;n++;}return Math.sqrt(sum/n);};
      let peak=0,nonfinite=0,clipped=0,dc=0,stereo=0;
      for(let i=0;i<left.length;i++){
        if(!Number.isFinite(left[i])||!Number.isFinite(right[i]))nonfinite++;
        peak=Math.max(peak,Math.abs(left[i]),Math.abs(right[i]));
        if(Math.abs(left[i])>=1||Math.abs(right[i])>=1)clipped++;
        dc+=(left[i]+right[i])/2;stereo+=Math.abs(left[i]-right[i]);
      }
      const metrics={title:MUSIC.title,bpm:MUSIC.bpm,durationSeconds:duration,sampleRate,channels:2,scheduledNotes:count,peak,nonfinite,clippedFrames:clipped,dcOffset:dc/left.length,meanStereoDifference:stereo/left.length,hangarRms:rms(2,7),flightRms:rms(11,22),boostRms:rms(26,32),endingRms:rms(duration-.05,duration),synthesis:'real Chromium OfflineAudioContext using game score and synth',deviceListeningTest:false};
      const bytes=new ArrayBuffer(44+buffer.length*4),view=new DataView(bytes);
      const text=(p,t)=>{for(let i=0;i<t.length;i++)view.setUint8(p+i,t.charCodeAt(i));};
      text(0,'RIFF');view.setUint32(4,bytes.byteLength-8,true);text(8,'WAVE');text(12,'fmt ');view.setUint32(16,16,true);view.setUint16(20,1,true);view.setUint16(22,2,true);view.setUint32(24,sampleRate,true);view.setUint32(28,sampleRate*4,true);view.setUint16(32,4,true);view.setUint16(34,16,true);text(36,'data');view.setUint32(40,buffer.length*4,true);
      for(let i=0;i<buffer.length;i++){view.setInt16(44+i*4,Math.round(Math.max(-1,Math.min(1,left[i]))*32767),true);view.setInt16(46+i*4,Math.round(Math.max(-1,Math.min(1,right[i]))*32767),true);}
      const array=new Uint8Array(bytes);let binary='';for(let i=0;i<array.length;i+=32768)binary+=String.fromCharCode(...array.subarray(i,i+32768));
      URL.revokeObjectURL(url);return {metrics,wav:btoa(binary)};
    }''',source)
    browser.close()
metrics=result['metrics']
(out/'neon-wake-preview.wav').write_bytes(base64.b64decode(result['wav']))
(out/'render-metrics.json').write_text(json.dumps(metrics,indent=2)+'\n')
assert metrics['nonfinite']==0 and metrics['clippedFrames']==0,metrics
assert .0001<metrics['hangarRms']<metrics['flightRms'],metrics
assert metrics['peak']<.98 and abs(metrics['dcOffset'])<.01,metrics
assert metrics['meanStereoDifference']>.0001,metrics
print(json.dumps(metrics,indent=2))
