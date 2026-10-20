"""Midface displacement: constrained landmarks, fold safety and UI persistence."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-midface-lift';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
results=[]
for name in ['leroy-chiao','chris-hadfield','mae-jemison','glasses']:
 recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text(encoding='utf-8'))
 source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=Image.open(source).size
 for p in recipe['people']:p['settings']['parameters']={}
 p=max(recipe['people'],key=lambda p:p['face'][2]*p['face'][3]);lm=np.array(p['landmarks'])[:,:2]
 for side in ['left','right']:
  p['settings']['parameters']={'face.midfaceLift.'+side:100}
  r=call(op='portrait_geometry',recipe=recipe,size=size,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'{name}-{side}.png'))
  assert not r['identity'],name
  w,h=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  fixed=np.linalg.norm((forward_points(field,lm)-lm)*size,axis=1)
  # Rendering mesh interpolation is approximate; fixed facial landmarks must
  # remain subpixel even at full strength.
  assert fixed.max()<.8,(name,side,'anchor drift',float(fixed.max()))
  dx=np.gradient(field,1/(w-1),axis=1);dy=np.gradient(field,1/(h-1),axis=0)
  determinant=(1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]
  assert determinant.min()>.25,(name,side,'fold',float(determinant.min()))
  indices=[46,35,13,62] if side=='left' else [41,31,3,58]
  center=sum(lm[i]*v for i,v in zip(indices,[.38,.25,.27,.10]))
  movement=np.linalg.norm((forward_points(field,center[None,:])-center)*size)
  assert movement>.5,(name,side,'no visible displacement',float(movement))
  nose=lm[27:36];lip=lm[[48,49,50,51,52,62,53,54,55,56,57,58]]
  islands=np.concatenate([np.array([island.mean(0)+(vertex-island.mean(0))*t for vertex in island for t in np.linspace(0,1,9)]) for island in [nose,lip]])
  island_drift=np.linalg.norm((forward_points(field,islands)-islands)*size,axis=1).max()
  assert island_drift<.3,(name,side,'fixed nose/lip island',float(island_drift))
  results.append(dict(photo=name,side=side,anchorDrift=float(fixed.max()),minimumJacobian=float(determinant.min()),motionPixels=float(movement),islandDrift=float(island_drift)))
 p['settings']['parameters']={'face.midfaceLift.left':100,'face.midfaceLift.right':100,'face.malarShape.left':100,'face.malarShape.right':100,'face.contourLift.left':100,'face.contourLift.right':100}
 rr=call(op='portrait_geometry',recipe=recipe,size=size,field=str(OUT/'combined.bin'),source=str(source),render=str(OUT/f'{name}-combined.png'));cw,ch=rr['meshSize'];cf=np.fromfile(OUT/'combined.bin',np.float32).reshape(ch,cw,4)[:,:,:2];cx=np.gradient(cf,1/(cw-1),axis=1);cy=np.gradient(cf,1/(ch-1),axis=0);cj=(1+cx[:,:,0])*(1+cy[:,:,1])-cy[:,:,0]*cx[:,:,1];assert cj.min()>.20,(name,'combined fold',float(cj.min()))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/semantic-forehead-volume/real-recipe.json').read_text(encoding='utf-8'))
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';real['people'][0]['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=real);wait();call(op='portrait_state',select=0,panel=0,linked=False,side=0);before=wait()['recipe']
call(op='portrait_state',parameter='face.midfaceLift',value=80);changed=wait()['recipe'];parameters=changed['people'][0]['settings']['parameters'];assert parameters.get('face.midfaceLift.left')==80 and parameters.get('face.midfaceLift.right',0)==0
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait()
r=call(op='portrait_state',source=str(source),render=str(OUT/'real.png'),save=str(OUT/'midface-lift.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,{'roundtrip':r['roundtrip'],'regionError':r['regionError']}
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=changed);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'real.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');(OUT/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print('Midface anchor protection, nonfolding fields, bilateral UI, undo, ROI and persistence passed')
