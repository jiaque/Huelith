"""D07 real clothed walking legs, shoe pixels and non-target people."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,base64,io
OUT=ROOT/'docs/test-evidence/M3/semantic-legs'
BASE=ROOT/'docs/test-evidence/M3/quality';source=BASE/'FudanPed00001-source.png'
call(op='init',settings=str(OUT/'real-settings'))
base=json.loads((BASE/'FudanPed00001-recipe.json').read_text());size=np.array(Image.open(source).size);scale=size-1;records=[]
# Independent image-space shoe interiors, not pose-generated proxies.
shoes=np.array([(175,403),(185,411),(271,424),(287,423),(432,475),(452,475),(519,451),(519,469)])/scale
for index in range(len(base['people'])):
    for side in range(2):
        for value in [-100,50,100]:
            recipe=copy.deepcopy(base)
            for p in recipe['people']:p['settings']['parameters']={}
            p=recipe['people'][index];p['settings']['limbGeometryVersion']=2;p['settings']['parameters']={'body.legWidth.'+('left' if side==0 else 'right'):value}
            try:r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'real-field.bin'),source=str(source),render=str(OUT/f'walking-{index}-{side}-{value}.png'))
            except RuntimeError as e:
                records.append(dict(person=index,side=side,value=value,refused=str(e)));print(records[-1],flush=True);continue
            w,h=r['meshSize'];f=np.fromfile(OUT/'real-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
            drift=float(np.linalg.norm((forward_points(f,shoes)-shoes)*scale,axis=1).max())
            assert drift<.3,(index,side,value,'shoe moved',drift)
            assert not r['identity']
            pose=np.array(p['settings']['pose']);mask=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['bodyMask']))).convert('L'))
            joints=pose[np.array([23,25,27])+side,:2]
            joint_drift=float(np.linalg.norm((forward_points(f,joints)-joints)*scale,axis=1).max());assert joint_drift<.3
            shifts=[]
            for first in [23,25]:
                a,b=pose[[first+side,first+2+side],:2]*scale;center=(a+b)*.5;axis=(b-a)/np.linalg.norm(b-a);normal=np.array([-axis[1],axis[0]])
                other_center=pose[[first+1-side,first+3-side],:2].mean(0)*scale
                normal*=1 if normal@(center-other_center)>=0 else -1
                for distance in np.arange(1,np.linalg.norm(b-a),.5):
                    q=center+normal*distance;xy=np.clip((q/scale*np.array(mask.shape[::-1])).astype(int),0,np.array(mask.shape[::-1])-1)
                    if mask[xy[1],xy[0]]<128:break
                q=q/scale;shift=float(((forward_points(f,np.array([q]))[0]-q)*scale)@normal);shifts.append(shift)
            assert max(abs(v) for v in shifts)>.05,(index,side,value,'no visible contour effect',shifts)
            assert all(v*value<=.01 for v in shifts),(index,side,value,'wrong contour direction',shifts)
            records.append(dict(person=index,side=side,value=value,shoeDrift=drift,jointDrift=joint_drift,outerContourShifts=shifts));print(records[-1],flush=True)
(OUT/'walking-results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
assert len(records)==12 and all('shoeDrift' in r for r in records),'Incomplete real clothed-leg cases'
def wait():
    until=time.monotonic()+40
    while time.monotonic()<until:
        call(op='pump',waitMs=20);state=call(op='portrait_state')
        if state['ready'] and not state['previewBusy']:
            assert state['displayedRevision']==state['previewRevision'],state['status']
            return state
    raise AssertionError('preview timeout')
recipe=copy.deepcopy(base)
for p in recipe['people']:p['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=recipe);wait()
for index in range(len(base['people'])):
    call(op='portrait_state',select=index,panel=3,linked=True);before=wait()['recipe']
    call(op='portrait_state',parameter='body.legWidth',value=60);state=wait()
    assert state['recipe']['people'][index]['settings']['parameters']['body.legWidth.left']==60
    call(op='portrait_state',history=-1);assert wait()['recipe']==before
    call(op='portrait_state',history=1);wait()
saved=call(op='portrait_state',source=str(source),render=str(OUT/'walking-workflow.png'),save=str(OUT/'walking.huelith'),checkRegion=True)
assert saved['roundtrip'] and saved['regionError']<2e-6
final=wait()['recipe'];call(op='portrait_close')
call(op='portrait_window',path=str(source),recipe=final);wait()
call(op='portrait_state',source=str(source),render=str(OUT/'walking-reopened.png'))
assert np.array_equal(np.asarray(Image.open(OUT/'walking-workflow.png')),np.asarray(Image.open(OUT/'walking-reopened.png')))
call(op='portrait_close')
call(op='shutdown')
