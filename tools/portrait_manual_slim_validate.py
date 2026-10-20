from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/manual-slim';OUT.mkdir(exist_ok=True)
source=ROOT/'research/m3/portraits/jassi-standing.jpg';size=np.array(Image.open(source).size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text())
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);s=call(op='portrait_state')
  if s['ready'] and not s['previewBusy'] and s['displayedRevision']==s['previewRevision']:return s
 raise AssertionError(s['status'])
call(op='init',settings=str(OUT/'settings'));call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',panel=3,select=0,click='portraitManualSlimRegion');wait()
call(op='portrait_state',paintPath=[[.25,.42],[.7,.68]]);state=wait()
assert len(state['recipe']['people'][0]['settings']['manualSlimRegion'])==4,state['status']
records=[]
for amount in [-100,100]:
 call(op='portrait_state',parameter='body.manualSlim',value=amount);state=wait()
 assert state['recipe']['people'][0]['settings']['parameters']['body.manualSlim']==amount,state['status']
 r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'jassi-standing-{amount}.png'))
 w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
 x,y,bw,bh=state['recipe']['people'][0]['settings']['manualSlimRegion']
 points=np.array([[x+bw*.25,y+bh*.5],[x+bw*.75,y+bh*.5],[x+bw*.5,y+bh*.5]])
 motion=(forward_points(f,points)-points)*(size-1)
 assert motion[0,0]*amount>0 and motion[1,0]*amount<0,motion
 assert np.max(abs(motion[:,1]))<.01 and np.linalg.norm(motion[2])<.1
 yy,xx=np.mgrid[0:h,0:w];outside=(xx/(w-1)<x-.005)|(xx/(w-1)>x+bw+.005)|(yy/(h-1)<y-.005)|(yy/(h-1)>y+bh+.005)
 assert np.max(abs(f[outside]))<1e-8,'outside region moved'
 dy,dx=np.gradient(f,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min());assert jac>.1
 records.append(dict(amount=amount,leftPixels=float(motion[0,0]),rightPixels=float(motion[1,0]),minimumJacobian=jac))
before=state['recipe'];box=np.array(before['people'][0]['settings']['manualSlimRegion'])
call(op='portrait_state',paintPath=[[float(box[0]+box[2]),float(box[1]+box[3])],[.76,.72]])
state=wait();changed=np.array(state['recipe']['people'][0]['settings']['manualSlimRegion']);assert changed[2]>box[2]+.04 and changed[3]>box[3]+.02
before=state['recipe'];center=changed[:2]+changed[2:]*.5
call(op='portrait_state',paintPath=[center.tolist(),(center+[.03,.01]).tolist()],paintEnd='ungrab');assert wait()['recipe']==before
call(op='portrait_state',paintPath=[center.tolist(),(center+[.03,.01]).tolist()]);state=wait()
moved=np.array(state['recipe']['people'][0]['settings']['manualSlimRegion']);assert np.max(abs(moved[2:]-changed[2:]))<.002
assert abs(moved[0]-changed[0]-.03)<.003
r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'moved-field.bin'))
mw,mh=r['meshSize'];moved_field=np.fromfile(OUT/'moved-field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
probe=np.array([[.735,.56]])
assert np.linalg.norm(sample(f,probe)*(size-1))<.01
assert np.linalg.norm(sample(moved_field,probe)*(size-1))>.5,'resizing box did not expand actual warp support'
saved=state['recipe'];call(op='portrait_state',doubleClickParameter='body.manualSlim');assert wait()['recipe']['people'][0]['settings']['parameters']['body.manualSlim']==0
call(op='portrait_state',history=-1);assert wait()['recipe']==saved
r=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'slim.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',project=str(OUT/'slim.huelith'));assert wait()['recipe']==saved
call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'))
assert np.array_equal(np.array(Image.open(OUT/'saved.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_state',panel=0,parameter='face.width',value=15);state=wait()
assert state['recipe']['people'][0]['settings']['parameters']['face.width']==15,state['status']
call(op='portrait_state',panel=4);wait()
fx,fy,fw,fh=recipe['people'][0]['face']
call(op='portrait_state',paintPath=[[fx+fw*.3,fy+fh*.65],[fx+fw*.35,fy+fh*.65]]);state=wait()
assert state['recipe']['people'][0]['settings'].get('liquify'),state['status']
call(op='portrait_state',panel=3,parameter='body.manualSlim',value=40);state=wait()
assert state['recipe']['people'][0]['settings']['parameters']['body.manualSlim']==40,state['status']
before=state['recipe'];call(op='portrait_state',click='portraitManualSlimRegion');wait()
call(op='portrait_state',paintPath=[[.9,.85],[.905,.855]]);state=wait()
assert state['recipe']==before,'invalid tiny box poisoned active adjustment'
call(op='portrait_state',parameter='body.manualSlim',value=35);assert wait()['recipe']['people'][0]['settings']['parameters']['body.manualSlim']==35
(OUT/'results.json').write_text(json.dumps(records,indent=2));call(op='portrait_close');call(op='shutdown');print(records)
