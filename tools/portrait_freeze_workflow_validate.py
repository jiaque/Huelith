"""Freeze must preserve the current visible deformation, including on thaw."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/liquify-freeze';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png'
size=np.array(Image.open(source).size)
recipe=json.loads((source.parent/'leroy-chiao-recipe.json').read_text())
for p in recipe['people']:
 p['settings']['parameters']={};p['settings'].pop('liquify',None);p['settings'].pop('protection',None)
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);s=call(op='portrait_state')
  if s['ready'] and not s['previewBusy'] and s['displayedRevision']==s['previewRevision']:return s
 raise AssertionError(s['status'])
def field(name):
 state=wait();r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/(name+'.bin')),source=str(source),render=str(OUT/(name+'.png')))
 w,h=r['meshSize']
 if not w or not h:
  mw,mh=np.maximum(2,np.ceil(size*min(1,512/max(size))).astype(int)+1)
  return np.zeros((mh,mw,2),np.float32)
 return np.fromfile(OUT/(name+'.bin'),np.float32).reshape(h,w,4)[:,:,:2]
call(op='init',settings=str(OUT/'settings'))
call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',select=0,panel=4,brushSize=140);wait()
fx,fy,fw,fh=recipe['people'][0]['face']
start=np.array([fx+fw*.3,fy+fh*.65]);end=start+[fw*.25,0]
call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);before=field('before-freeze')
call(op='portrait_state',click='portraitFreeze1',brushSize=500)
call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);frozen=field('after-freeze')
call(op='portrait_state',liquifyTool=0,brushSize=100)
call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);edited=field('edited-frozen')
call(op='portrait_state',click='portraitFreeze2',brushSize=500)
call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);thawed=field('after-thaw')
call(op='portrait_state',liquifyTool=0,brushSize=100)
call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);continued=field('continued')
delta=lambda a,b:float(np.max(np.linalg.norm((a-b)*(size-1),axis=2)))
result=dict(initialMotionPixels=delta(before,0),freezeChangedExistingPixels=delta(frozen,before),frozenEditPixels=delta(edited,frozen),thawChangedExistingPixels=delta(thawed,edited),afterThawEditPixels=delta(continued,thawed))
saved=wait()['recipe']
r=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'freeze.huelith'),checkRegion=True)
assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',project=str(OUT/'freeze.huelith'));assert wait()['recipe']==saved
call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'))
assert np.array_equal(np.asarray(Image.open(OUT/'saved.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',select=0,panel=0,parameter='face.width',value=12);baseline=field('face-before')
call(op='portrait_state',click='portraitFreeze1',brushSize=140)
call(op='portrait_state',paint=start.tolist());face_frozen=field('face-frozen')
assert delta(baseline,face_frozen)<.05
call(op='portrait_state',parameter='face.width',value=20);face_edited=field('face-edited')
probe=start[None,:]
result['faceFrozenTargetPixels']=float(np.linalg.norm((sample(face_edited,probe)-sample(face_frozen,probe))*(size-1)))
result['faceUnprotectedEditPixels']=delta(face_edited,face_frozen)
call(op='portrait_state',click='portraitFreeze2',brushSize=140)
call(op='portrait_state',paint=start.tolist());face_thawed=field('face-thawed')
result['faceThawPixels']=delta(face_thawed,face_edited)
call(op='portrait_state',history=-1);undo=field('face-undo-thaw')
assert delta(undo,face_edited)<.05
call(op='portrait_state',history=1);redo=field('face-redo-thaw')
assert delta(redo,face_thawed)<.05
(OUT/'results.json').write_text(json.dumps(result,indent=2))
call(op='portrait_close');call(op='shutdown');print(result)
assert result['initialMotionPixels']>2
assert result['freezeChangedExistingPixels']<.05,'Painting freeze must not undo existing deformation'
assert result['frozenEditPixels']<.05,'Frozen region must reject new displacement'
assert result['thawChangedExistingPixels']<.05,'Thaw must only change protection, not visible deformation'
assert result['afterThawEditPixels']>2,'Thawed region must remain editable'
assert result['faceFrozenTargetPixels']<.05
assert result['faceUnprotectedEditPixels']>.2
assert result['faceThawPixels']<.05
