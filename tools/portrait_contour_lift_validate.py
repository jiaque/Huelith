"""Contour lift displacement: constrained landmarks, fold safety and UI persistence."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-contour-lift';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
results=[]
for name in ['leroy-chiao','chris-hadfield','mae-jemison','glasses']:
 recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text(encoding='utf-8'))
 source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=Image.open(source).size
 for p in recipe['people']:p['settings']['parameters']={}
 p=max(recipe['people'],key=lambda p:p['face'][2]*p['face'][3]);lm=np.array(p['landmarks'])[:,:2]
 for side in ['left','right']:
  p['settings']['parameters']={'face.contourLift.'+side:100}
  r=call(op='portrait_geometry',recipe=recipe,size=size,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'{name}-{side}.png'))
  assert not r['identity'],name
  w,h=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  fixed_indices=list(range(17,66))+[0,8,16]+(list(range(1,8)) if side=="left" else list(range(9,16)))
  fixed=np.linalg.norm((forward_points(field,lm[fixed_indices])-lm[fixed_indices])*size,axis=1)
  # Rendering mesh interpolation is approximate; fixed facial landmarks must
  # remain subpixel even at full strength.
  assert fixed.max()<.8,(name,side,'anchor drift',float(fixed.max()))
  dx=np.gradient(field,1/(w-1),axis=1);dy=np.gradient(field,1/(h-1),axis=0)
  determinant=(1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]
  assert determinant.min()>.25,(name,side,'fold',float(determinant.min()))
  index=12 if side=='left' else 4
  eye_axis=(lm[42:48].mean(0)-lm[36:42].mean(0))*size;eye_axis/=np.linalg.norm(eye_axis);down=np.array([-eye_axis[1],eye_axis[0]])
  movement=(forward_points(field,lm[[index]])-lm[[index]])*size;upward=-float(movement[0]@down);outward=float(movement[0]@eye_axis)*(1 if side=='left' else -1)
  facewidth=np.linalg.norm((lm[16]-lm[0])*size)
  assert upward>.015*facewidth and outward>.003*facewidth,(name,side,'wrong direction',upward,outward)
  # Dense points across a neck/clothing band, below the chin along face axes.
  neck=np.array([lm[8]+(eye_axis*x+down*y)*facewidth/size for x in np.linspace(-.35,.35,12) for y in np.linspace(.10,.50,12)])
  neck_drift=np.linalg.norm(sample(field,neck)*size,axis=1).max()
  assert neck_drift<.25,(name,side,'neck/clothing drift',float(neck_drift))
  movement=upward
  results.append(dict(photo=name,side=side,anchorDrift=float(fixed.max()),minimumJacobian=float(determinant.min()),motionPixels=float(movement),neckDrift=float(neck_drift)))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/semantic-forehead-volume/real-recipe.json').read_text(encoding='utf-8'))
unreliable=copy.deepcopy(real);unreliable['people'][0]['settings']['parameters']={'face.contourLift.left':100};unreliable['people'][0]['landmarks'][12][2]=.01
assert call(op='portrait_geometry',recipe=unreliable,size=[512,512])['identity']
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';real['people'][0]['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=real);wait();call(op='portrait_state',select=0,panel=0,linked=False,side=0);before=wait()['recipe']
call(op='portrait_state',parameter='face.contourLift',value=80);changed=wait()['recipe'];parameters=changed['people'][0]['settings']['parameters'];assert parameters.get('face.contourLift.left')==80 and parameters.get('face.contourLift.right',0)==0
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait()
r=call(op='portrait_state',source=str(source),render=str(OUT/'real.png'),save=str(OUT/'contour-lift.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,{'roundtrip':r['roundtrip'],'regionError':r['regionError']}
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=changed);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'real.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');(OUT/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print('Contour lift anchor protection, nonfolding fields, bilateral UI, undo, ROI and persistence passed')
