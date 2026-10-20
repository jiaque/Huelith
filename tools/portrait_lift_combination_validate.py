"""Actual asynchronous one-click UI, stale/cancel gates and displayed preview."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,base64,io
OUT=ROOT/'docs/test-evidence/M3/semantic-lift-combination';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png'
real=json.loads((ROOT/'docs/test-evidence/M3/semantic-forehead-volume/real-recipe.json').read_text(encoding='utf-8'));real['people'][0]['settings']['parameters']={'feature.noseWidth':12}
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=real);wait();call(op='portrait_state',panel=0,select=0);before=wait();assert 'face.foreheadLift' not in before['recipe']['people'][0]['settings']['parameters']
start=time.monotonic();call(op='portrait_state',click='portraitLiftCombination');dispatch=time.monotonic()-start;assert dispatch<.5,dispatch
applied=wait();s=applied['recipe']['people'][0]['settings'];params=s['parameters'];assert params['face.foreheadLift']==25 and params['face.midfaceLift.left']==30 and params['face.contourLift.left']==30 and params['face.orbitalLift.left']==25 and params['face.perioralLift.left']==25 and params['feature.noseWidth']==12
assert applied['history']==before['history']+1
call(op='portrait_state',history=-1);assert wait()['recipe']==before['recipe'];call(op='portrait_state',history=1);assert wait()['recipe']==applied['recipe']
# Reapplication replaces targets instead of stacking another warp.
call(op='portrait_state',click='portraitLiftCombination');repeated=wait();assert repeated['recipe']==applied['recipe']
r=call(op='portrait_state',source=str(source),render=str(OUT/'combined.png'),save=str(OUT/'lift.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
(OUT/'recipe.json').write_text(json.dumps(applied['recipe']),encoding='utf-8')
# Stale work must not overwrite a later user edit; cancel does not apply a
# partial analysis. Both run against the production asynchronous callback.
call(op='portrait_state',click='portraitLiftCombination');call(op='portrait_state',parameter='face.width',value=7);stale=call(op='portrait_state')['recipe'];assert wait()['recipe']==stale
call(op='portrait_state',click='portraitLiftCombination');call(op='portrait_state',click='portraitCancelCompute');assert wait()['recipe']==stale
call(op='portrait_close')
# Missing independent skin ranges are explicitly skipped, not substituted by
# whole-face guesses, and do not clear existing editing settings.
missing=copy.deepcopy(real);missing['people'][0]['settings']['automaticMasks'].pop('faceSkin',None)
call(op='portrait_window',path=str(source),recipe=missing);wait();call(op='portrait_state',panel=0,select=0);unknown=wait()['recipe'];call(op='portrait_state',click='portraitLiftCombination');assert wait()['recipe']==unknown;call(op='portrait_close')

# Complete occlusion or an empty skin mask must not start contour solving.
for mask_name,level in [('faceSkin',0),('eyeglassFrame',255)]:
 blocked=copy.deepcopy(real);buffer=io.BytesIO();Image.new('L',(64,64),level).save(buffer,format='PNG');blocked['people'][0]['settings']['automaticMasks'][mask_name]=base64.b64encode(buffer.getvalue()).decode()
 call(op='portrait_window',path=str(source),recipe=blocked);wait();call(op='portrait_state',panel=0,select=0);unchanged=wait()['recipe'];call(op='portrait_state',click='portraitLiftCombination');assert wait()['recipe']==unchanged;call(op='portrait_close')
# All lift controls at maximum use a bounded, nonfolding combined field.
for name,recipe_path in [('leroy-chiao','semantic-forehead-volume/real-recipe.json'),('chris-hadfield','semantic-tear-volume/chris-hadfield-recipe.json'),('john-glenn','semantic-tear-volume/john-glenn-recipe.json')]:
 recipe=json.loads((ROOT/'docs/test-evidence/M3'/recipe_path).read_text(encoding='utf-8'));p=recipe['people'][0];p['settings']['parameters']={'face.foreheadLift':100}
 for key in ['contourLift','midfaceLift','orbitalLift','perioralLift']:
  for side in ['left','right']:p['settings']['parameters']['face.'+key+'.'+side]=100
 source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=Image.open(source).size
 fields=[]
 for strength in [100,50,0]:
  p['settings']['strength']=strength
  r=call(op='portrait_geometry',recipe=recipe,size=size,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'{name}-{strength}.png'))
  if strength==0:assert r['identity'];continue
  w,h=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2];fields.append(field)
  dx=np.gradient(field,1/(w-1),axis=1);dy=np.gradient(field,1/(h-1),axis=0);jac=(1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0];assert jac.min()>.2,(name,float(jac.min()))
 assert np.linalg.norm(fields[1])<np.linalg.norm(fields[0])*.65,name
call(op='shutdown');print('Lift combination UI, replacement, single undo, stale/cancel, missing skin, ROI/save and three-portrait full-strength fields passed')
