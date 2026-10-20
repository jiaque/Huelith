"""Exercise production inverse fields and inspect real portrait crops offscreen."""
import os, ctypes, json, time
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M3/semantic-contour'
OUT.mkdir(parents=True,exist_ok=True)
os.environ['QT_QPA_PLATFORM']='offscreen'
os.environ['QT_PLUGIN_PATH']='D:/Program Files/Qt/6.10.3/msvc2022_64/plugins'
handles=[os.add_dll_directory('D:/Program Files/Qt/6.10.3/msvc2022_64/bin'),os.add_dll_directory(str(ROOT/'build/windows-release'))]
dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
command=dll.huelith_python_command;command.argtypes=[ctypes.c_char_p];command.restype=ctypes.c_char_p
def call(**kw):
    result=json.loads(command(json.dumps(kw).encode()))
    if 'error' in result:raise RuntimeError(result['error'])
    return result
def sample(field,p):
    h,w=field.shape[:2];q=np.clip(p,0,1)*[w-1,h-1]
    a=np.floor(q).astype(int);b=np.minimum(a+1,[w-1,h-1]);f=q-a
    return ((field[a[:,1],a[:,0]]*(1-f[:,0,None])+field[a[:,1],b[:,0]]*f[:,0,None])*(1-f[:,1,None])+
            (field[b[:,1],a[:,0]]*(1-f[:,0,None])+field[b[:,1],b[:,0]]*f[:,0,None])*f[:,1,None])
def forward_points(field,points):
    # Solve q + inverse_displacement(q) = source. Simple fixed-point iteration
    # can oscillate at strong but invertible warps, falsely reporting drift.
    q=points.copy();eps=1e-5
    for _ in range(50):
        residual=q+sample(field,q)-points
        if np.max(np.abs(residual))<1e-8:return q
        dx=(sample(field,q+[eps,0])-sample(field,q-[eps,0]))/(2*eps)
        dy=(sample(field,q+[0,eps])-sample(field,q-[0,eps]))/(2*eps)
        jac=np.stack([dx,dy],axis=2)+np.eye(2)
        step=np.linalg.solve(jac,residual[...,None])[...,0]
        gain=np.ones((len(q),1));before=np.linalg.norm(residual,axis=1)
        for _ in range(12):
            candidate=q-step*gain
            worse=np.linalg.norm(candidate+sample(field,candidate)-points,axis=1)>before+1e-12
            if not worse.any():break
            gain[worse]*=.5
        q-=step*gain
    assert np.max(np.abs(q+sample(field,q)-points))<1e-7,'inverse field root did not converge'
    return q

OUT=ROOT/'docs/test-evidence/M3/semantic-back';source=OUT/'postural-kyphosis.png';call(op='init',settings=str(OUT/'manual-settings'));call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'))
def wait():
 end=time.monotonic()+120
 while time.monotonic()<end:
  call(op='pump',waitMs=30);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError('preview timeout')
r=wait();print('count',r['count'],'status',r['status'],flush=True);assert r['count']>0
(OUT/'postural-recipe.json').write_text(json.dumps(r['recipe']),encoding='utf-8')
call(op='portrait_state',panel=3,select=0);before=wait()['recipe'];call(op='portrait_state',click='portraitBackLocate')
size=np.array(Image.open(source).size);points=np.array([[365,74],[496,225],[459,382]])/(size-1)
for point in points:call(op='portrait_state',paint=point.tolist())
r=wait();assert len(r['recipe']['people'][0]['settings'].get('backContour',[]))==3,r['status']
located=r['recipe'];call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);assert wait()['recipe']==located
call(op='portrait_state',parameter='body.back',value=100);r=wait();print(r['status'],flush=True);assert r['recipe']['people'][0]['settings']['parameters'].get('body.back')==100
result=call(op='portrait_state',source=str(source),render=str(OUT/'postural-after.png'),save=str(OUT/'postural.huelith'),checkRegion=True);assert result['roundtrip'] and result['regionError']<2e-6
recipe=r['recipe'];call(op='portrait_state',click='portraitBackClear');cleared=wait()['recipe']['people'][0]['settings'];assert 'backContour' not in cleared and cleared['parameters']['body.back']==0;call(op='portrait_state',history=-1);assert wait()['recipe']==recipe;call(op='portrait_close')
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',source=str(source),render=str(OUT/'postural-reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'postural-after.png')),np.array(Image.open(OUT/'postural-reopened.png')));call(op='portrait_close')
result=call(op='portrait_geometry',recipe=recipe,source=str(source),size=size.tolist(),field=str(OUT/'postural-field.bin'))
w,h=result['meshSize'];f=np.fromfile(OUT/'postural-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
shift=(forward_points(f,points)-points)*(size-1);print('point shifts',shift.round(3).tolist(),flush=True);assert np.linalg.norm(shift[0])<.15 and np.linalg.norm(shift[2])<.15;assert shift[1,0]<-3
fixed=np.array([[260,150],[310,200],[220,400],[350,460],[200,50]])/(size-1);assert np.linalg.norm((forward_points(f,fixed)-fixed)*(size-1),axis=1).max()<.15
dx=np.gradient(f,1/(w-1),axis=1);dy=np.gradient(f,1/(h-1),axis=0);minimum=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0]).min());assert minimum>.5
(OUT/'postural-validation.json').write_text(json.dumps({'manualContour':True,'source':'postural-kyphosis.png','contourShiftPixels':shift.tolist(),'minimumJacobian':minimum,'fixedHeadChestPelvis':True,'undoRedoClear':True,'saveReopen':True,'regionConsistent':True},indent=2))
# Partial portraits must retain detected background lines. Verify protected
# samples inside the active back strip, not only distant unaffected pixels.
settings=recipe['people'][0]['settings']
assert settings.get('backgroundLines'), 'partial recognition dropped background lines'
import base64,io,copy
skin=Image.open(io.BytesIO(base64.b64decode(settings['automaticMasks']['skin']))).convert('L');skinArray=np.asarray(skin)
targets=[]
for line in settings['backgroundLines']:
 a=np.array(line[:2]);b=np.array(line[2:])
 # Lines crossing the person are deliberately excluded; they are not
 # background constraints even when one end extends outside the person.
 probes=np.array([a+(b-a)*t for t in np.linspace(0,1,9)])
 xy=np.minimum((probes*np.array(skin.size)).astype(int),np.array(skin.size)-1)
 if np.max(skinArray[xy[:,1],xy[:,0]])>=25:continue
 for t in np.linspace(0,1,101):
  q=a+(b-a)*t;xy=np.minimum((q*np.array(skin.size)).astype(int),np.array(skin.size)-1)
  if skinArray[xy[1],xy[0]]<25:targets.append(q)
assert targets
unprotected=copy.deepcopy(recipe);unprotected['people'][0]['settings']['protectBackground']=False
raw=call(op='portrait_geometry',recipe=unprotected,size=size.tolist(),field=str(OUT/'unprotected-field.bin'))
rw,rh=raw['meshSize'];uf=np.fromfile(OUT/'unprotected-field.bin',np.float32).reshape(rh,rw,4)[:,:,:2]
q=np.array(targets);active=np.linalg.norm(sample(uf,q)*(size-1),axis=1)>.5
assert active.any(), 'no background line crosses active displacement'
protectedMax=float(np.linalg.norm(sample(f,q[active])*(size-1),axis=1).max())
unprotectedMax=float(np.linalg.norm(sample(uf,q[active])*(size-1),axis=1).max())
assert protectedMax<.15,protectedMax
(OUT/'background-line-validation.json').write_text(json.dumps({'activeSamples':int(active.sum()),'protectedMaxPixels':protectedMax,'unprotectedMaxPixels':unprotectedMax},indent=2))
print('Background line displacement',protectedMax,'versus',unprotectedMax,flush=True)

portable=call(op='portrait_geometry',portableSkin=settings,bodyPreset=True,targetSettings=settings)['portable']
assert portable['backMode']=='manual' and 'backContour' not in portable
try:
 call(op='portrait_geometry',portableSkin=portable,bodyPreset=True,targetSettings={})
 raise AssertionError('manual preset silently changed to automatic correction')
except RuntimeError as e:assert '定位上背' in str(e)
call(op='shutdown');print('Real cropped side photograph, manual contour UI, posterior correction, fixed head/chest/pelvis, ROI and save passed')
