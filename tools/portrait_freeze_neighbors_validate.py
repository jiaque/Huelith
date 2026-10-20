"""Actual freeze interaction before edits, with a second visible person."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],str(Path('tools/portrait_freeze_workflow_validate.py').resolve()),'exec'))
import base64,io
source=ROOT/'research/m2/instances/FudanPed00001.png';size=np.array(Image.open(source).size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/FudanPed00001-recipe.json').read_text())
for p in recipe['people']:
 p['settings']['parameters']={}
 for k in list(p['settings']):
  if k.startswith('protection') or k=='liquify':p['settings'].pop(k)
call(op='init',settings=str(OUT/'neighbors-settings'))
call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',select=1,panel=4);before=field('neighbors-before')
x,y,w,h=recipe['people'][1]['body'];point=[x+w*.5,y+h*.55]
call(op='portrait_state',click='portraitFreeze1',brushSize=50)
call(op='portrait_state',paint=point);frozen=field('neighbors-freeze-first')
assert np.array_equal(before,frozen)
call(op='portrait_state',liquifyTool=0,brushSize=90)
call(op='portrait_state',paintPath=[point,[point[0]+.025,point[1]]]);after=field('neighbors-edited')
mask=np.array(Image.open(io.BytesIO(base64.b64decode(recipe['people'][0]['bodyMask']))).resize(tuple(size),Image.Resampling.NEAREST))>200
a=np.asarray(Image.open(OUT/'neighbors-before.png'));b=np.asarray(Image.open(OUT/'neighbors-edited.png'))
assert np.array_equal(a[mask],b[mask]),'Freeze/edit moved the neighboring person'
error=float(np.linalg.norm((sample(after,np.array([point]))-sample(frozen,np.array([point])))*(size-1)))
motion=float(np.max(np.linalg.norm(after*(size-1),axis=2)))
assert error<.05 and motion>.1,(error,motion)
(OUT/'neighbors.json').write_text(json.dumps(dict(frozenTargetPixels=error,unprotectedMotionPixels=motion,neighborPixelDifference=0),indent=2))
call(op='portrait_close');call(op='shutdown');print(error,motion)
