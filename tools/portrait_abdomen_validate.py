from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-abdomen';OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg';recipe=json.loads((source.parent/'mediapipe-pose-recipe.json').read_text(encoding='utf-8'))
call(op='init',settings=str(OUT/'settings'));p=recipe['people'][0];p['settings']['waistGeometryVersion']=2
pose=np.array(p['settings']['pose']);size=np.array(Image.open(source).size)
a=pose[[11,12],:2].mean(0)*size;b=pose[[23,24],:2].mean(0)*size;axis=b-a;axis/=np.linalg.norm(axis);normal=np.array([-axis[1],axis[0]])
mid=a+(b-a)*.725;width=np.linalg.norm((pose[11,:2]-pose[12,:2])*size)*.44;probes=np.array([mid-normal*width,mid+normal*width])/size
for value in [0,50,100]:
 p['settings']['parameters']={'body.abdomen':value}
 r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'abdomen-{value}.png'))
 if value==0:assert r['identity'];continue
 mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
 joints=pose[[11,12,13,14,15,16,23,24],:2];drift=np.linalg.norm((forward_points(field,joints)-joints)*size,axis=1).max();assert drift<.75,drift
 output=forward_points(field,probes)*size;ratio=np.linalg.norm(output[1]-output[0])/(width*2);assert (ratio-1)*value<0,ratio
 assert ratio<1,ratio
 dx=np.gradient(field,1/(mw-1),axis=1);dy=np.gradient(field,1/(mh-1),axis=0);jac=(1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0];assert jac.min()>.5,float(jac.min())
 fixed=np.array([a+(b-a)*t+normal*n for t in [.45,.725,.95] for n in ([-width,0,width] if t!=.725 else [0])])/size
 assert np.linalg.norm((forward_points(field,fixed)-fixed)*size,axis=1).max()<.2
 print(value,ratio,drift,float(jac.min()),flush=True)
# Waist and abdominal targets remain independently reversible and do not fold.
p['settings']['parameters']={'body.waist':100,'body.abdomen':100}
r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'combined.bin'),source=str(source),render=str(OUT/'combined.png'));mw,mh=r['meshSize'];f=np.fromfile(OUT/'combined.bin',np.float32).reshape(mh,mw,4)[:,:,:2];dx=np.gradient(f,1/(mw-1),axis=1);dy=np.gradient(f,1/(mh-1),axis=0);assert ((1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0]).min()>.4
# Actual control, undo, source-space ROI and persistence.
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
p['settings']['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=3,select=0);before=wait()['recipe'];call(op='portrait_state',parameter='body.abdomen',value=80);after=wait();assert after['recipe']['people'][0]['settings']['parameters'].get('body.abdomen')==80,after['status']
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait()
r=call(op='portrait_state',source=str(source),render=str(OUT/'real.png'),save=str(OUT/'abdomen.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after['recipe']);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'real.png')),np.array(Image.open(OUT/'reopened.png')));call(op='portrait_close')
# Uncertain profile and crossing arms reject instead of moving the back/arm.
import copy
for case in ['profile','crossing','missing']:
 bad=copy.deepcopy(recipe);bp=bad['people'][0];bp['settings']['parameters']={}
 if case=='profile':bp['settings']['pose'][12][:2]=bp['settings']['pose'][11][:2]
 elif case=='crossing':
  for i,q in zip([13,15],[a+(b-a)*.725-normal*width*2,a+(b-a)*.725+normal*width*2]):bp['settings']['pose'][i]=[float(q[0]/size[0]),float(q[1]/size[1]),1]
 else:bp['bodyMask']=''
 call(op='portrait_window',path=str(source),recipe=bad);wait();call(op='portrait_state',panel=3,select=0);before=wait()['recipe'];call(op='portrait_state',parameter='body.abdomen',value=80);blocked=wait();assert blocked['recipe']==before,case;call(op='portrait_close')
call(op='shutdown');print('Abdominal boundary inward motion, fixed centre/chest/hips, nonfolding map, UI/save and rejection cases passed')
