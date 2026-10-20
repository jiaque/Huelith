"""Production portrait controls and FP32 renderer, hosted by the M0 Python DLL harness."""
import json,tempfile,time
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
exec(Path('tools/m3_people_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as t:')[0])
O=R/'docs/test-evidence/M3/geometry';O.mkdir(parents=True,exist_ok=True)
def checked(**args):
 result=call(**args)
 assert 'error' not in result,result
 return result
def settle():
 deadline=time.monotonic()+60
 while time.monotonic()<deadline:
  checked(op='pump',waitMs=40);r=checked(op='portrait_state')
  if r['ready'] and r['count']:return r
 raise AssertionError('portrait identification timeout')
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp)
 source=O/'portrait-source.png'
 im=Image.open(R/'research/m0/portrait-highres.jpg');im.thumbnail((1100,1100));im.save(source)
 checked(op='portrait_window',path=str(source),models=str(R/'build/windows-release/models'));r=settle()
 assert r['count']==1
 checked(op='portrait_state',select=0,panel=0)
 assert checked(op='portrait_state',source=str(source),render=str(O/'neutral.png'))['identity']
 checked(op='portrait_state',parameter='face.width',value=70)
 r=checked(op='portrait_state',source=str(source),render=str(O/'face-width.png'),checkRegion=True)
 assert not r['identity'] and r['regionError']<2e-6,r.get('regionError')
 checked(op='portrait_state',source=str(source),render=str(O/'half-amount.png'),amount=.5)
 checked(op='portrait_state',parameter='face.width',value=35)
 checked(op='portrait_state',source=str(source),render=str(O/'half-parameter.png'))
 assert np.array_equal(np.array(Image.open(O/'half-amount.png')),np.array(Image.open(O/'half-parameter.png'))),'intensity crossfade/field mismatch'
 checked(op='portrait_state',parameter='face.width',value=0)
 checked(op='portrait_state',panel=1,linked=False,side=0,parameter='feature.eyeSize',value=70)
 checked(op='portrait_state',source=str(source),render=str(O/'left-eye.png'))
 params=checked(op='portrait_state')['recipe']['people'][0]['settings']['parameters']
 assert params.get('feature.eyeSize.left')==70 and params.get('feature.eyeSize.right',0)==0
 recipe=checked(op='portrait_state')['recipe'];(O/'recipe.json').write_text(json.dumps(recipe,ensure_ascii=False,indent=2),encoding='utf-8')
 checked(op='pump',waitMs=600)
 checked(op='portrait_state',save=str(O/'portrait.huelith'),source=str(source),capture=str(O/'window.png'))
 before=checked(op='portrait_state',panel=4)['recipe']
 assert checked(op='portrait_state',liquify=True,nestedAccept=False)['recipe']==before
 after=checked(op='portrait_state',liquify=True,nestedAccept=True)['recipe']
 assert after!=before and after['people'][0]['settings'].get('liquify')
 assert checked(op='portrait_state',history=-1)['recipe']==before
 checked(op='portrait_close')
 checked(op='portrait_window',path=str(source),models=str(R/'build/windows-release/models'),recipe=recipe)
 checked(op='portrait_state',source=str(source),render=str(O/'reopened.png'))
 assert np.array_equal(np.array(Image.open(O/'left-eye.png')),np.array(Image.open(O/'reopened.png')))
 checked(op='portrait_close')
 source=R/'research/m2/instances/FudanPed00001.png'
 checked(op='portrait_window',path=str(source),models=str(R/'build/windows-release/models'));r=settle();assert r['count']==2
 print('pose point counts',[len(p['settings'].get('pose',[])) for p in r['recipe']['people']],flush=True)
 for p in r['recipe']['people']:assert len(p['settings'].get('pose',[]))==33
 checked(op='portrait_state',select=1,panel=3,parameter='body.waist',value=80)
 result=checked(op='portrait_state',source=str(source),render=str(O/'body.png'));assert not result['identity']
 checked(op='portrait_state',source=str(source),render=str(O/'body-neutral.png'),amount=0)
 original=np.asarray(Image.open(O/'body-neutral.png')).astype(int);modified=np.asarray(Image.open(O/'body.png')).astype(int)
 # Face and interior body of the unselected person must not be deformed.
 protected=np.abs(original[180:350,205:255]-modified[180:350,205:255]);assert protected.max()==0,protected.max()
 r=checked(op='portrait_state');(O/'body-recipe.json').write_text(json.dumps(r['recipe'],indent=2))
 checked(op='portrait_close');checked(op='shutdown')
 print('M3 geometry core PASS',flush=True)
