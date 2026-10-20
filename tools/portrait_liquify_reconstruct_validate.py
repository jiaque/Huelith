"""Local reconstruction after thaw, through real portrait mouse events."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],str(Path('tools/portrait_freeze_workflow_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/liquify-reconstruct';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
call(op='portrait_window',project=str(ROOT/'docs/test-evidence/M3/liquify-freeze/freeze.huelith'));wait()
call(op='portrait_state',select=0,panel=0,parameter='face.width',value=12);wait()
call(op='portrait_state',panel=4);initial=field('before')
saved=copy.deepcopy(wait()['recipe'])
call(op='portrait_state',click='portraitLiquifyReset');automatic=field('automatic-only')
call(op='portrait_state',history=-1);wait()
fx,fy,fw,fh=saved['people'][0]['face'];center=np.array([fx+fw*.46,fy+fh*.65])
call(op='portrait_state',liquifyTool=1,brushSize=80)
history=wait()['history']
for i in range(10):call(op='portrait_state',paint=center.tolist());wait()
after=field('reconstructed')
probe=center[None,:]
before_error=float(np.linalg.norm((sample(initial,probe)-sample(automatic,probe))*(size-1)))
after_error=float(np.linalg.norm((sample(after,probe)-sample(automatic,probe))*(size-1)))
(OUT/'target-probe.json').write_text(json.dumps(dict(beforePixels=before_error,afterPixels=after_error,status=wait()['status'],committedStrokes=wait()['history']-history),ensure_ascii=False,indent=2))
# The narrow brush cannot erase a 35px displacement while keeping its 40px
# boundary fixed and the inverse map injective. It must nevertheless accept
# safe progress, rather than reject every stroke as the regression did.
assert before_error>2 and after_error<before_error*.8,(before_error,after_error,wait()['status'],wait()['history']-history)
h,w=after.shape[:2];yy,xx=np.mgrid[:h,:w];distance=np.hypot((xx/(w-1)-center[0])*(size[0]-1),(yy/(h-1)-center[1])*(size[1]-1))
outside=float(np.max(np.linalg.norm((after-initial)*(size-1),axis=2)[distance>43]))
assert outside<.05,outside
assert history<wait()['history']<=history+10
call(op='portrait_state',brushSize=240)
for i in range(10):call(op='portrait_state',paint=center.tolist());wait()
wide=field('reconstructed-wide')
wide_error=float(np.linalg.norm((sample(wide,probe)-sample(automatic,probe))*(size-1)))
assert wide_error<before_error*.05,(before_error,wide_error)
r=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'local.huelith'),checkRegion=True)
assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',project=str(OUT/'local.huelith'));wait()
call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'))
assert np.array_equal(np.asarray(Image.open(OUT/'saved.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close')
fields=[]
for count,delay in [(2,0),(41,0),(41,35)]:
 call(op='portrait_window',path=str(source),recipe=saved);wait()
 call(op='portrait_state',select=0,panel=4,liquifyTool=1,brushSize=100);wait()
 end=center+[fw*.12,0];path=[(center+(end-center)*t).tolist() for t in np.linspace(0,1,count)]
 call(op='portrait_state',paintPath=path,paintStepMs=delay);fields.append(field(f'path-{count}-{delay}'))
 call(op='portrait_close')
density_error=max(float(np.max(np.linalg.norm((f-fields[0])*(size-1),axis=2))) for f in fields)
assert density_error<.05,density_error
call(op='portrait_window',path=str(source),recipe=saved);wait()
call(op='portrait_state',select=0,panel=4,click='portraitFreeze1',brushSize=500)
call(op='portrait_state',paint=center.tolist());protected=field('protected-before')
call(op='portrait_state',liquifyTool=1,brushSize=80)
for i in range(3):call(op='portrait_state',paint=center.tolist());wait()
protected_after=field('protected-after')
freeze_error=float(np.max(np.linalg.norm((protected_after-protected)*(size-1),axis=2)))
assert freeze_error<.05,freeze_error
call(op='portrait_close')
result=dict(targetManualPixelsBefore=before_error,targetManualPixelsAfter=after_error,wideBrushTargetPixelsAfter=wide_error,outsideChangePixels=outside,eventDensityDifferencePixels=density_error)
result['frozenReconstructionPixels']=freeze_error
import base64,io
source=ROOT/'research/m2/instances/FudanPed00001.png';size=np.array(Image.open(source).size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/FudanPed00001-recipe.json').read_text())
for person in recipe['people']:person['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',select=1,panel=4,brushSize=60);original=field('neighbors-original')
x,y,w,h=recipe['people'][1]['body'];start=np.array([x+w*.5,y+h*.55]);end=start+[.015,0]
call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);warped=field('neighbors-warped')
call(op='portrait_state',liquifyTool=1,brushSize=120)
for i in range(8):call(op='portrait_state',paint=((start+end)*.5).tolist());wait()
restored=field('neighbors-restored')
neighbor=np.array(Image.open(io.BytesIO(base64.b64decode(recipe['people'][0]['bodyMask']))).resize(tuple(size),Image.Resampling.NEAREST))>200
a=np.asarray(Image.open(OUT/'neighbors-original.png'));b=np.asarray(Image.open(OUT/'neighbors-restored.png'))
assert np.array_equal(a[neighbor],b[neighbor])
target=((start+end)*.5)[None,:]
old=float(np.linalg.norm((sample(warped,target)-sample(original,target))*(size-1)))
new=float(np.linalg.norm((sample(restored,target)-sample(original,target))*(size-1)))
assert old>.2 and new<old*.05,(old,new)
result.update(neighborPixelDifference=0,noFreezeManualBeforePixels=old,noFreezeManualAfterPixels=new)
call(op='portrait_close')
(OUT/'results.json').write_text(json.dumps(result,indent=2));call(op='shutdown');print(result)
