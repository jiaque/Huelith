import ctypes,os,json,tempfile
from pathlib import Path
from PIL import Image,ImageDraw
R=Path.cwd(); O=R/'docs/test-evidence/M3/people';O.mkdir(parents=True,exist_ok=True)
h=[os.add_dll_directory('D:/Program Files/Qt/6.10.3/msvc2022_64/bin'),os.add_dll_directory(str(R/'build/windows-release'))]
os.environ['QT_PLUGIN_PATH']='D:/Program Files/Qt/6.10.3/msvc2022_64/plugins'
d=ctypes.CDLL(str(R/'build/windows-release/huelith_python_driver.dll'));f=d.huelith_python_command;f.argtypes=[ctypes.c_char_p];f.restype=ctypes.c_char_p
call=lambda **k:json.loads(f(json.dumps(k).encode()))
with tempfile.TemporaryDirectory() as t:
 print(call(op='init',settings=t))
 for path in [R/'research/m0/portrait-highres.jpg',R/'research/m2/instances/FudanPed00001.png']:
  r=call(op='portrait_probe',path=str(path),models=str(R/'build/windows-release/models'),maskPrefix=str(O/(path.stem+'-mask')))
  assert 'error' not in r,r
  assert len(r['people'])==(1 if path.suffix=='.jpg' else 2),r
  assert all(len(p['landmarks'])==66 and not p['bodyAmbiguous'] for p in r['people']),r
  (O/(path.stem+'.json')).write_text(json.dumps(r,indent=2));print(path.name,[(p['confidence'],p['bodyAmbiguous']) for p in r.get('people',[])],r.get('error'),flush=True)
  im=Image.open(path).convert('RGB');draw=ImageDraw.Draw(im)
  for p in r.get('people',[]):
   x,y,w,hh=p['face'];draw.rectangle((x*im.width,y*im.height,(x+w)*im.width,(y+hh)*im.height),outline='lime',width=3)
   for x,y,c in p['landmarks']:draw.ellipse((x*im.width-2,y*im.height-2,x*im.width+2,y*im.height+2),fill='red')
  im.thumbnail((1100,850));im.save(O/(path.stem+'.jpg'))
 path=R/'research/m2/instances/FudanPed00001.png'
 models=str(R/'build/windows-release/models')
 previous=r['people'];previous[0]['settings']={'skin':{'strength':.25}};previous[1]['settings']={'skin':{'strength':.75}}
 repeated=call(op='portrait_probe',path=str(path),models=models,previous=list(reversed(previous)))
 assert {p['id']:p['settings'] for p in repeated['people']}=={p['id']:p['settings'] for p in previous}
 ambiguous=call(op='portrait_probe',path=str(path),models=models,previous=[previous[0],dict(previous[0],id=previous[1]['id'])])
 assert ambiguous['people'][0]['matchAmbiguous'] and 'skin' not in ambiguous['people'][0]['settings']
 r=call(op='portrait_window',path=str(path),models=models)
 import time
 deadline=time.monotonic()+60
 while time.monotonic()<deadline:
  call(op='pump',waitMs=30);r=call(op='portrait_state')
  if r['ready'] and r['count']:break
 assert r['count']==2 and r['selected']==-1,{k:v for k,v in r.items() if k!='recipe'}
 zoom=r['zoom'];r=call(op='portrait_state',select=0);assert r['zoom']==zoom
 r=call(op='portrait_state',focus=1);assert r['zoom']>zoom
 r=call(op='portrait_state',select=1);assert r['focusMode']==1
 r=call(op='portrait_state',linked=False,side=1)
 assert r['recipe']['people'][1]['settings']['side']==1
 r=call(op='portrait_state',select=0)
 assert set(r['recipe']['people'][0]['settings'])<= {'pose','backgroundLines','automaticMasks','partialSkin','protectBackground'},set(r['recipe']['people'][0]['settings'])
 r=call(op='portrait_state',select=1)
 baseline=r['recipe'];zoom=r['zoom']
 for panel in range(16):
  r=call(op='portrait_state',panel=panel)
  assert r['recipe']==baseline and r['zoom']==zoom
 # Navigation does not consume undo steps; a real edit does.
 before=call(op='portrait_state');call(op='portrait_state',panel=5,parameter='skin.smooth',value=25)
 edited=call(op='portrait_state')['recipe']
 r=call(op='portrait_state',history=-1);assert r['recipe']==baseline and r['recipe']['people'][1]['settings']['side']==1
 r=call(op='portrait_state',history=1);assert r['recipe']==edited
 r=call(op='portrait_state',focus=2);assert r['focusMode']==2
 r=call(op='portrait_state',save=str(O/'people.huelith'),source=str(path),capture=str(O/'window.png'));assert r.get('roundtrip'),{k:v for k,v in r.items() if k!='recipe'}
 recipe=r['recipe'];call(op='portrait_close');r=call(op='portrait_window',path=str(path),models=models,recipe=recipe)
 assert r['ready'] and r['recipe']==recipe,r
 call(op='portrait_state',reject=True);call(op='portrait_close')
 call(op='window')
 independent=call(op='work_window_probe',kind='portrait',source=str(path),capture=str(O/'independent.png'))
 assert independent.get('stages')==4 and all(independent.get(k) for k in ['nonModal','unowned','mainEnabled','childMinimized','mainNotMinimized','mainEdited','childRestored','mainVisible','mainMinimizedIndependently']),independent
 for accept in [False,True]:
  session=call(op='work_window_probe',kind='portraitSession',source=str(path),recipe=recipe,accept=accept)
  assert session.get('changedInside') and session.get('revisionDelta')==int(accept) and session.get('changedAfter')==accept,session
  if accept:assert session['oneUndoRestores']
 (O/'acceptance.json').write_text(json.dumps(dict(passed=True,people=2,independentSettings=True,stableOnReorder=True,ambiguousRejected=True,singleSelect=True,focus=True,roundtrip=True),indent=2))
 print('M3 people acceptance PASS',flush=True)
 print(call(op='shutdown'))
