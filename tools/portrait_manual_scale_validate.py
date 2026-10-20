from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/manual-scale';OUT.mkdir(exist_ok=True)
source=ROOT/'research/m3/portraits/jassi-standing.jpg';size=np.array(Image.open(source).size);unit=min(size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text())
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);s=call(op='portrait_state')
  if s['ready'] and not s['previewBusy'] and s['displayedRevision']==s['previewRevision']:return s
 raise AssertionError(s['status'])
call(op='init',settings=str(OUT/'settings'));call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',panel=3,select=0,click='portraitManualScaleRegion');wait()
call(op='portrait_state',paintPath=[[.48,.51],[.68,.51]]);state=wait()
assert len(state['recipe']['people'][0]['settings']['manualScaleRegion'])==4,state['status']
records=[]
for amount in [-100,100]:
 call(op='portrait_state',parameter='body.manualScale',value=amount);state=wait()
 assert state['recipe']['people'][0]['settings']['parameters']['body.manualScale']==amount,state['status']
 r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'jassi-standing-{amount}.png'))
 w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
 cx,cy,inner,outer=state['recipe']['people'][0]['settings']['manualScaleRegion'];center=np.array([cx,cy])
 offsets=np.array([[inner*.25*unit,0],[-inner*.25*unit,0],[0,inner*.25*unit],[0,-inner*.25*unit]])/(size-1)
 points=center+offsets;actual=(forward_points(f,points)-center)*(size-1);expected=offsets*(size-1)*np.exp(amount*.0035)
 error=float(np.max(np.linalg.norm(actual-expected,axis=1)));assert error<.2,error
 assert np.linalg.norm((forward_points(f,center[None,:])-center)*(size-1))<.1
 yy,xx=np.mgrid[0:h,0:w];outside=np.hypot((xx/(w-1)-cx)*(size[0]-1),(yy/(h-1)-cy)*(size[1]-1))>outer*unit+2
 assert np.max(abs(f[outside]))<1e-8
 dy,dx=np.gradient(f,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min());assert jac>.1
 records.append(dict(amount=amount,innerScaleErrorPixels=error,minimumJacobian=jac))
# Drag the inner ring at the top: outer radius and centre must stay unchanged.
old=np.array(state['recipe']['people'][0]['settings']['manualScaleRegion']);start=old[:2]+[0,-old[2]*unit/(size[1]-1)]
call(op='portrait_state',paintPath=[start.tolist(),(start+[0,-.015]).tolist()]);state=wait();inner=np.array(state['recipe']['people'][0]['settings']['manualScaleRegion'])
assert inner[2]>old[2]+.02 and abs(inner[3]-old[3])<1e-8 and np.max(abs(inner[:2]-old[:2]))<1e-8
start=inner[:2]+[inner[3]*unit/(size[0]-1),0]
call(op='portrait_state',paintPath=[start.tolist(),(start+[.05,0]).tolist()]);state=wait();outer=np.array(state['recipe']['people'][0]['settings']['manualScaleRegion'])
assert outer[3]>inner[3]+.04 and abs(outer[2]-inner[2])<1e-8
before=state['recipe'];start=outer[:2]+[.005,0]
call(op='portrait_state',paintPath=[start.tolist(),(start+[.02,.01]).tolist()],paintEnd='ungrab');assert wait()['recipe']==before
call(op='portrait_state',paintPath=[start.tolist(),(start+[.02,.01]).tolist()]);state=wait();moved=np.array(state['recipe']['people'][0]['settings']['manualScaleRegion'])
assert np.max(abs(moved[:2]-outer[:2]-[.02,.01]))<.003 and np.max(abs(moved[2:]-outer[2:]))<1e-8
saved=state['recipe'];call(op='portrait_state',doubleClickParameter='body.manualScale');assert wait()['recipe']['people'][0]['settings']['parameters']['body.manualScale']==0
call(op='portrait_state',history=-1);assert wait()['recipe']==saved
r=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'scale.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',project=str(OUT/'scale.huelith'));assert wait()['recipe']==saved
call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'saved.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_state',panel=0,parameter='face.width',value=12);assert wait()['recipe']['people'][0]['settings']['parameters']['face.width']==12
call(op='portrait_state',panel=3,click='portraitManualScaleRegion');wait()
call(op='portrait_state',parameter='body.manualScale',value=-40);state=wait();assert state['recipe']['people'][0]['settings']['parameters']['body.manualScale']==-40
# Different inner radii must change the effective strong area while keeping
# the outer boundary fixed, including a narrow transition ring.
import copy
surfaces=[]
for inner_radius in [.03,.10,.19]:
 for amount in [-100,100]:
  trial=copy.deepcopy(recipe);trial['people'][0]['settings']['parameters']={'body.manualScale':amount}
  trial['people'][0]['settings']['manualScaleRegion']=[.48,.51,inner_radius,.2]
  r=call(op='portrait_geometry',size=size.tolist(),recipe=trial,field=str(OUT/'rings.bin'))
  w,h=r['meshSize'];field=np.fromfile(OUT/'rings.bin',np.float32).reshape(h,w,4)[:,:,:2]
  dy,dx=np.gradient(field,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min());assert jac>0
  surfaces.append(dict(inner=inner_radius,amount=amount,minimumJacobian=jac))
(OUT/'rings.json').write_text(json.dumps(surfaces,indent=2))
(OUT/'results.json').write_text(json.dumps(records,indent=2));call(op='portrait_close');call(op='shutdown');print(records)
