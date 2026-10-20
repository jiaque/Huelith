from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/manual-distort';OUT.mkdir(exist_ok=True)
source=ROOT/'research/m3/portraits/jassi-standing.jpg';size=np.array(Image.open(source).size);unit=min(size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text())
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);s=call(op='portrait_state')
  if s['ready'] and not s['previewBusy'] and s['displayedRevision']==s['previewRevision']:return s
 raise AssertionError(s['status'])
call(op='init',settings=str(OUT/'settings'));call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',panel=3,select=0,click='portraitManualDistortRegion');wait()
call(op='portrait_state',paintPath=[[.48,.51],[.68,.51]]);state=wait()
cx,cy,radius=state['recipe']['people'][0]['settings']['manualDistortRegion'];center=np.array([cx,cy]);radius*=unit
records=[]
for key in ['body.manualRotate','body.manualShear']:
 for amount in [-100,100]:
  for k in ['body.manualRotate','body.manualShear']:call(op='portrait_state',parameter=k,value=amount if k==key else 0)
  state=wait();assert state['recipe']['people'][0]['settings']['parameters'][key]==amount,state['status']
  r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'jassi-standing-{key}-{amount}.png'))
  w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  offsets=np.array([[radius*.2,0],[0,-radius*.2],[-radius*.2,0],[0,radius*.2]])
  actual=(forward_points(f,center+offsets/(size-1))-center)*(size-1)
  if key.endswith('Rotate'):
   angle=amount/100*np.pi/6;matrix=np.array([[np.cos(angle),-np.sin(angle)],[np.sin(angle),np.cos(angle)]])
  else:matrix=np.array([[1,.4*amount/100],[0,1]])
  error=float(np.max(np.linalg.norm(actual-offsets@matrix.T,axis=1)));assert error<.2,error
  yy,xx=np.mgrid[0:h,0:w];outside=np.hypot((xx/(w-1)-cx)*(size[0]-1),(yy/(h-1)-cy)*(size[1]-1))>radius+2
  assert np.max(abs(f[outside]))<1e-8
  dy,dx=np.gradient(f,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min());assert jac>.1
  records.append(dict(key=key,amount=amount,targetErrorPixels=error,minimumJacobian=jac))
call(op='portrait_state',click='portraitManualDistortReset');wait()
# Actual circular rotation handle at 35% radius.
start=center+[radius*.35/(size[0]-1),0];end=center+np.array([np.cos(.2),np.sin(.2)])*radius*.35/(size-1)
before=wait();path=[(start+(end-start)*t).tolist() for t in np.linspace(0,1,18)]
frames=call(op='portrait_state',paintPath=path,paintStepMs=35)['paintFrames'];state=wait()
assert max(frames[:-1])>frames[0],'no preview updated while handle was down'
assert 30<state['recipe']['people'][0]['settings']['parameters']['body.manualRotate']<50,state['status']
assert state['history']==before['history']+1,'drag must be one undo step'
# Square shear handle at top, horizontal motion produces a shear.
start=center+[0,-radius*.35/(size[1]-1)];end=start+[.015,0]
call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);state=wait()
assert state['recipe']['people'][0]['settings']['parameters']['body.manualShear']< -20,state['status']
before=state['recipe'];call(op='portrait_state',paintPath=[center.tolist(),(center+[.02,.01]).tolist()],paintEnd='ungrab');assert wait()['recipe']==before
call(op='portrait_state',panel=0,parameter='face.width',value=12);wait();call(op='portrait_state',panel=3);wait()
before=wait()['recipe'];call(op='portrait_state',click='portraitManualDistortReset');state=wait();p=state['recipe']['people'][0]['settings']['parameters']
assert p['body.manualRotate']==0 and p['body.manualShear']==0 and p['face.width']==12
call(op='portrait_state',history=-1);assert wait()['recipe']==before
r=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'distort.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'distort.huelith'));assert wait()['recipe']==saved
call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'saved.png')),np.array(Image.open(OUT/'reopened.png')))
(OUT/'results.json').write_text(json.dumps(records,indent=2));call(op='portrait_close');call(op='shutdown');print(records)
