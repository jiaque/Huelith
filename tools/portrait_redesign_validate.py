from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/approved-redesign';O.mkdir(parents=True,exist_ok=True)
def wait():
 for _ in range(1500):
  checked(op='pump',waitMs=20);s=checked(op='portrait_state')
  if s['ready'] and not s['previewBusy']:return s
 raise AssertionError('timeout')
with tempfile.TemporaryDirectory(dir=R/"build",ignore_cleanup_errors=True) as tmp:
 checked(op='init',settings=tmp)
 base=json.loads((R/'docs/test-evidence/M3/quality/portrait-highres-recipe.json').read_text());source=R/'docs/test-evidence/M3/quality/portrait-highres-source.png'
 start=time.monotonic();checked(op='portrait_window',path=str(source),recipe=base);open_time=time.monotonic()-start
 settle();checked(op='portrait_state',panel=1);before=wait()
 r=checked(op='portrait_state',dragParameter='feature.eyeSize',dragValues=list(range(0,65))+list(range(65,19,-1)),stepMs=20)
 frames=r['dragFrames'];assert len({str(f['viewport']) for f in frames})==1,'layout jitter';assert len({f['zoom'] for f in frames})==1,'zoom jitter';assert len({f['frames'] for f in frames})>3,'no intermediate previews'
 r=wait();assert r['history']==before['history']+1;assert r['displayedRevision']==r['previewRevision'];checked(op='portrait_state',history=-1);wait()
 face=r['recipe']['people'][0]['face'];x=face[0]+face[2]*.7;y=face[1]+face[3]*.65
 points=[[x+i*.001,y] for i in range(30)]
 checked(op='portrait_state',panel=4);h=checked(op='portrait_state')['history'];r=checked(op='portrait_state',stroke=points);assert r['recipe']['people'][0]['settings'].get('liquify'),'default push failed';assert r['history']==h+1;wait();checked(op='portrait_state',capture=str(O/'liquify.png'))
 checked(op='portrait_state',history=-1);wait();checked(op='portrait_state',panel=8);h=checked(op='portrait_state')['history'];r=checked(op='portrait_state',stroke=points);strokes=r['recipe']['people'][0]['settings']['repairs'];assert len(strokes[-1]['points'])>10,'drag lost';assert r['history']==h+1
 wait();checked(op='portrait_state',capture=str(O/'wrinkle.png'))
 for panel in range(16):
  checked(op='portrait_state',panel=panel);wait();checked(op='portrait_state',capture=str(O/f'panel-{panel:02}.png'))
 (O/'blockers.json').write_text(json.dumps({'openSecondsIncludingImport':open_time,'frames':frames,'strokePoints':len(strokes[-1]['points']),'passed':True},indent=2))
 print('PASS blockers and 16 panels',open_time,flush=True)
 checked(op='portrait_close');checked(op='shutdown')
