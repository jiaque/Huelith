"""Verify localized cheek volume rather than radial whole-face inflation."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-cheek-volume';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'geometry-settings'))
results=[]
for name in ['leroy-chiao','chris-hadfield','mae-jemison','glasses']:
 recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text(encoding='utf-8'));source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=Image.open(source).size
 for p in recipe['people']:p['settings']['parameters']={}
 p=max(recipe['people'],key=lambda p:p['face'][2]*p['face'][3]);lm=np.array(p['landmarks'])[:,:2];p['settings']['cheekVolumeVersion']=2
 axis=(lm[42:48].mean(0)-lm[36:42].mean(0))*size;axis/=np.linalg.norm(axis)
 fixed=[i for i in range(66) if i not in [2,3,13,14]]
 for amount in [-100,100]:
  p['settings']['parameters']={'face.volume':amount};r=call(op='portrait_geometry',recipe=recipe,size=size,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'{name}-{amount}.png'));assert not r['identity']
  w,h=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2];moved=forward_points(field,lm);drift=np.linalg.norm((moved-lm)*size,axis=1)
  assert drift[fixed].max()<.8,(name,amount,'fixed anchors',float(drift[fixed].max()))
  signed=((moved[[2,3,13,14]]-lm[[2,3,13,14]])*size)@axis*np.array([-1,-1,1,1])*np.sign(amount)
  # The target is a fraction of face width, not a minimum pixel count: a
  # small face must not receive a disproportionately large deformation.
  face_width=np.linalg.norm((lm[16]-lm[0])*size)
  assert signed.min()>.005*face_width,(name,amount,'outward/inward motion',signed.tolist())
  dx=np.gradient(field,1/(w-1),axis=1);dy=np.gradient(field,1/(h-1),axis=0);jac=(1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]
  assert jac.min()>.25,(name,amount,'fold',float(jac.min()))
  results.append(dict(photo=name,amount=amount,anchorDrift=float(drift[fixed].max()),minimumJacobian=float(jac.min()),arcMotion=signed.tolist()))
 # Semantic volume has no hidden generic cheek exposure in the style stage.
 call(op='portrait_geometry',recipe=recipe,source=str(source),styleOnly=str(OUT/'style-active.png'))
 p['settings']['parameters']={};call(op='portrait_geometry',recipe=recipe,source=str(source),styleOnly=str(OUT/'style-neutral.png'))
 assert np.array_equal(np.array(Image.open(OUT/'style-active.png')),np.array(Image.open(OUT/'style-neutral.png')))
 # Engine marker is portable, so loading a preset cannot restore old inflation.
 p['settings']['parameters']={'face.volume':70};r=call(op='portrait_geometry',portableSkin=p['settings'],facePreset=True,targetSettings={})
 assert r['portable']['cheekVolumeVersion']==2 and r['merged']['cheekVolumeVersion']==2
call(op='shutdown');(OUT/'geometry-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print('Cheek arc direction, cheekbone/jaw/feature protection, nonfolding, no legacy exposure and preset engine persistence passed')
