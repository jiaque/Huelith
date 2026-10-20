from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/liquify-twirl';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';size=np.array(Image.open(source).size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text())
for p in recipe['people']:p['settings']['parameters']={};p['settings'].pop('liquify',None)
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);s=call(op='portrait_state')
  if s['ready'] and not s['previewBusy'] and s['displayedRevision']==s['previewRevision']:return s
 raise AssertionError(s['status'])
call(op='init',settings=str(OUT/'settings'));fields=[];records=[]
fx,fy,fw,fh=recipe['people'][0]['face'];start=np.array([fx+fw*.2,fy+fh*.65]);end=start+[fw*.35,0]
for count,delay in [(2,0),(41,0),(41,35)]:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=4,select=0,brushSize=100,liquifyTool=3);before=wait()
 path=[(start+(end-start)*t).tolist() for t in np.linspace(0,1,count)]
 state=call(op='portrait_state',paintPath=path,paintStepMs=delay);frames=state['paintFrames'];state=wait()
 assert state['recipe']['people'][0]['settings'].get('liquify'),state['status']
 assert state['history']==before['history']+1
 r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'{count}-{delay}.png'))
 w,h=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2];fields.append(field)
 motion=float(np.linalg.norm((forward_points(field,np.array([(start+end)*.5]))-(start+end)*.5)*(size-1)));assert motion>.2
 if delay:assert max(frames[:-1])>frames[0],'no frame during drag'
 records.append(dict(events=count,delayMs=delay,targetMotionPixels=motion))
 call(op='portrait_close')
maximum=max(float(np.max(np.linalg.norm((f-fields[0])*(size-1),axis=2))) for f in fields[1:])
click_fields=[]
for repetitions in [1,41]:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=4,select=0,brushSize=100,liquifyTool=3);before=wait()
 center=(start+end)*.5
 call(op='portrait_state',paintPath=[center.tolist()]*repetitions);state=wait()
 assert state['history']==before['history']+1
 r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'click.bin'))
 w,h=r['meshSize'];click_fields.append(np.fromfile(OUT/'click.bin',np.float32).reshape(h,w,4)[:,:,:2])
 call(op='portrait_close')
assert np.array_equal(click_fields[0],click_fields[1]),'stationary events repeatedly applied bloat'
# Repeated explicit clicks remain valid, opaque, and saveable.
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=4,select=0,brushSize=100,liquifyTool=3);wait()
minimum=1
for repeat in range(12):
 call(op='portrait_state',paint=center.tolist());state=wait()
 assert state['recipe']['people'][0]['settings'].get('liquify'),state['status']
 r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'repeat.bin'))
 w,h=r['meshSize'];f=np.fromfile(OUT/'repeat.bin',np.float32).reshape(h,w,4)[:,:,:2]
 dy,dx=np.gradient(f,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min());minimum=min(minimum,jac);assert jac>0
r=call(op='portrait_state',source=str(source),render=str(OUT/'repeated.png'),save=str(OUT/'twirl.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
assert np.min(np.array(Image.open(OUT/'repeated.png'))[:,:,3])==255,'bloat introduced transparent hole'
saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'twirl.huelith'));assert wait()['recipe']==saved
call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'repeated.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close')
import base64
# Coordinate image verifies the production radial kernel itself: fixed centre,
# outward feature motion, unchanged exterior, and preserved alpha/HDR data.
yy,xx=np.mgrid[:128,:160];pixels=np.ones((128,160,4),np.float32);pixels[:,:,0]=xx/160;pixels[:,:,1]=yy/128;pixels[:,:,2]=2
request=dict(op='brush_probe',liquify=True,width=160,height=128,data=base64.b64encode(pixels.astype('<f4').tobytes()).decode(),dabs=[dict(tool=3,size=70,density=.5,pressure=1,**{'from':[80,64],'to':[80,64]})])
raw=call(**request);out=np.frombuffer(base64.b64decode(raw['data']),dtype='<f4').reshape(pixels.shape)
assert np.max(abs(out[64,80]-pixels[64,80]))<1e-6
assert out[64,90,1]<pixels[64,90,1] and out[64,70,1]>pixels[64,70,1]
point=np.array([out[64,90,0]*160,out[64,90,1]*128]);assert abs(np.linalg.norm(point-[80,64])-10)<.02
request['dabs'][0]['reverse']=True
raw=call(**request);reverse=np.frombuffer(base64.b64decode(raw['data']),dtype='<f4').reshape(pixels.shape)
assert reverse[64,90,1]>pixels[64,90,1] and reverse[64,70,1]<pixels[64,70,1]
assert np.array_equal(out[:20],pixels[:20]) and np.all(out[:,:,2]==2) and np.all(out[:,:,3]==1)
stress=[]
for reverse_direction in [False,True]:
 request['dabs']=[dict(tool=3,size=70,density=1,pressure=1,reverse=reverse_direction,**{'from':[80,64],'to':[80,64]})]*30
 raw=call(**request);out=np.frombuffer(base64.b64decode(raw['data']),dtype='<f4').reshape(pixels.shape)
 coords=out[:,:,:2]*[160,128];dy,dx=np.gradient(coords,axis=(0,1));jac=dx[:,:,0]*dy[:,:,1]-dy[:,:,0]*dx[:,:,1]
 radius=np.hypot(xx-80,yy-64);warped_radius=np.linalg.norm(coords-[80,64],axis=2)
 ring=(radius>3)&(radius<35)
 radial_error=float(np.max(abs(warped_radius[ring]-radius[ring])))
 assert radial_error<1,(reverse_direction,radial_error)
 assert np.min(jac)>.04 and np.linalg.norm(coords[64,80]-[80,64])<.01
 stress.append(dict(reverse=reverse_direction,maximumRadiusError=radial_error,minimumJacobian=float(np.min(jac))))
(OUT/'rings.json').write_text(json.dumps(stress,indent=2))
# Alt is handled by the actual portrait mouse event, not only the core API.
directions=[]
for alt in [False,True]:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=4,select=0,brushSize=100,liquifyTool=3);wait()
 call(op='portrait_state',paint=center.tolist(),alt=alt);state=wait()
 r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'direction.bin'))
 w,h=r['meshSize'];field=np.fromfile(OUT/'direction.bin',np.float32).reshape(h,w,4)[:,:,:2]
 probe=center+np.array([15,0])/(size-1)
 directions.append(float(((forward_points(field,probe[None,:])-probe)*(size-1))[0,1]))
 call(op='portrait_close')
assert directions[0]>.2 and directions[1]<-.2,directions
(OUT/'results.json').write_text(json.dumps(dict(runs=records,maximumFieldDifferencePixels=maximum,repeatedMinimumJacobian=minimum),indent=2))
call(op='shutdown');assert maximum<.05,maximum
print(records,'maximum field difference',maximum)
