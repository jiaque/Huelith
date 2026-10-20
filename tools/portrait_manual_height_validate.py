from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/manual-height';OUT.mkdir(exist_ok=True)
source=ROOT/'research/m3/portraits/jassi-standing.jpg';size=np.array(Image.open(source).size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text())
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);s=call(op='portrait_state')
  if s['ready'] and not s['previewBusy'] and s['displayedRevision']==s['previewRevision']:return s
 raise AssertionError(s['status'])
call(op='init',settings=str(OUT/'settings'));call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',panel=3,select=0,click='portraitManualHeightRange');wait()
for p in [[.48,.55],[.48,.84]]:call(op='portrait_state',paint=p)
state=wait();assert len(state['recipe']['people'][0]['settings']['manualHeightRange'])==2,state['status']
call(op='portrait_state',parameter='body.manualHeight',value=50);state=wait()
assert state['recipe']['people'][0]['settings']['parameters']['body.manualHeight']==50,state['status']
r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'field.bin'))
w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
points=np.array([[.48,.55],[.48,.84],[.48,.19],[.45,.20],[.52,.20]])
motion=(forward_points(f,points)-points)*(size-1)
assert motion[0,1]<-20 and np.linalg.norm(motion[1])<.5,motion
assert np.max(np.abs(motion[2:]-motion[2]))<.5,motion
original=np.array(state['recipe']['people'][0]['settings']['manualHeightRange'])
# Grab away from the line's centre; horizontal movement changes the angle
# without snapping the centre to the initial click position.
start=forward_points(f,np.array([original[1]]))[0]+[.05,0]
call(op='portrait_state',paintPath=[start.tolist(),(start+[.02,-.01]).tolist()]);state=wait()
changed=np.array(state['recipe']['people'][0]['settings']['manualHeightRange'])
assert abs(changed[1,0]-original[1,0]-.02)*size[0]<3,(original,changed)
assert np.linalg.norm((changed[0]-original[0])*size)<.5
before=state['recipe'];call(op='portrait_state',doubleClickParameter='body.manualHeight');assert wait()['recipe']['people'][0]['settings']['parameters']['body.manualHeight']==0
call(op='portrait_state',history=-1);assert wait()['recipe']==before
# Interrupted range drags must leave both stored geometry and the next gesture intact.
for ending in ['ungrab','deactivate']:
 call(op='portrait_state',click='portraitManualHeightRange');state=wait()
 r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'field.bin'))
 w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
 lower=np.array(state['recipe']['people'][0]['settings']['manualHeightRange'][1])
 start=forward_points(f,lower[None,:])[0]
 call(op='portrait_state',paintPath=[start.tolist(),(start+[.02,-.02]).tolist()],paintEnd=ending)
 assert wait()['recipe']==before,'interrupted range drag committed'
 call(op='portrait_state',paintPath=[start.tolist(),start.tolist()]);wait()
 assert np.max(abs(np.array(wait()['recipe']['people'][0]['settings']['manualHeightRange'])-np.array(before['people'][0]['settings']['manualHeightRange'])))<.002
r=call(op='portrait_state',source=str(source),render=str(OUT/'jassi-standing-height.png'),save=str(OUT/'height.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'height.huelith'));assert wait()['recipe']==saved
call(op='portrait_state',source=str(source),render=str(OUT/'jassi-standing-reopened.png'))
assert np.array_equal(np.array(Image.open(OUT/'jassi-standing-height.png')),np.array(Image.open(OUT/'jassi-standing-reopened.png')))
# A rejected range must not poison the remaining face/body session.
call(op='portrait_state',panel=3,select=0,click='portraitManualHeightRange');wait()
r=call(op='portrait_geometry',size=size.tolist(),recipe=saved,field=str(OUT/'field.bin'))
w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
upper=np.array(saved['people'][0]['settings']['manualHeightRange'][0]);start=forward_points(f,upper[None,:])[0]
call(op='portrait_state',paintPath=[start.tolist(),[.48,.16]])
state=wait();assert state['recipe']==saved,state['status']
call(op='portrait_state',parameter='body.manualHeight',value=25);assert wait()['recipe']['people'][0]['settings']['parameters']['body.manualHeight']==25
call(op='portrait_close');call(op='shutdown');print('Range lines, interruption recovery, stretch, rigid face, history and actual project reopen passed.')
