"""Visual interaction evidence through the production Qt window."""
from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
import sys,os,platform
O=R/'docs/test-evidence/M3/interaction-optimization';O.mkdir(parents=True,exist_ok=True)
base=json.loads((R/'docs/test-evidence/M3/quality/portrait-highres-recipe.json').read_text())
source=R/'docs/test-evidence/M3/quality/portrait-highres-source.png'
def settled():
 start=time.monotonic()
 while time.monotonic()-start<40:
  checked(op='pump',waitMs=30);s=checked(op='portrait_state')
  if s['ready'] and not s['previewBusy']:return s
 raise AssertionError('preview timeout')
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp)
 if '--raw' in sys.argv:
  checked(op='window')
  r=checked(op='work_window_probe',kind='raw',source=str(R/'research/m0/samples/r10-5871.CR3'))
  assert r['nativeIcon'] and r['windowIcon'] and r['childMinimized'] and r['mainNotMinimized'] and r['mainMinimizedIndependently'],r
  (O/'raw-window.json').write_text(json.dumps(r,indent=2));print('RAW native logo and independent minimize PASS',flush=True)
 else:
  checked(op='portrait_window',path=str(source),recipe=base)
  checked(op='portrait_state',select=0,panel=1,resize=[1100,760]);r=settled()
  assert r['windowIcon'] and r['acceptVisible'],r
  scale=round(r['dpr']*100);checked(op='portrait_state',capture=str(O/('dpi-'+str(scale)+'.png')))
  checked(op='portrait_state',click='portraitScopeHeader');checked(op='pump',waitMs=60)
  r=checked(op='portrait_state',control='portraitFreeze1');assert r['acceptVisible']
  if '--record' in sys.argv:
   checked(op='portrait_state',click='portraitScopeHeader',panel=5)
   capture=O/'drag-recording';r=checked(op='portrait_state',dragParameter='skin.smooth',dragValues=list(range(0,81,2))+list(range(80,19,-2)),stepMs=30,dragCaptureDirectory=str(capture));settled()
   files=sorted(capture.glob('frame-*.png'));assert len(files)>3
   frames=[Image.open(f).convert('RGB') for f in files];times=[int(f.stem.split('-')[1]) for f in files]
   durations=[max(20,times[i+1]-times[i]) for i in range(len(times)-1)]+[500]
   frames[0].save(O/'live-preview.gif',save_all=True,append_images=frames[1:],duration=durations,loop=0)
   # Compare at actual pixels while preview tasks may still be running.
   checked(op='portrait_state',focus=3,comparePressed=True);checked(op='pump',waitMs=150);checked(op='portrait_state',comparePressed=False);r=settled();assert not r['comparing']
   (O/'visual-performance.json').write_text(json.dumps({'sourceSize':Image.open(source).size,'dpr':r['dpr'],'system':platform.platform(),'processor':platform.processor(),'samples':r['dragFrames']},indent=2))
  if '--color-check' in sys.argv:
   results=[]
   for panel,key in [(6,'color.warmth'),(11,'makeup.lip')]:
    checked(op='portrait_state',panel=panel);before=settled();h=before['history']
    r=checked(op='portrait_state',dragParameter=key,dragValues=list(range(0,81,2))+list(range(80,19,-2)),stepMs=30)
    assert len({x['frames'] for x in r['dragFrames']})>3
    assert all(x['history']==h for x in r['dragFrames'])
    final=settled();assert final['history']==h+1 and final['displayedRevision']==final['previewRevision']
    results.append({'parameter':key,'samples':r['dragFrames']})
   (O/'color-drag.json').write_text(json.dumps(results,indent=2))
  checked(op='portrait_close');print('Layout/native logo at DPR',scale,'PASS',flush=True)
 checked(op='shutdown')
