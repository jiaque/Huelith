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

OUT=ROOT/'docs/test-evidence/M3/semantic-back';source=OUT/'full-standing-mirrored.png'
Image.open(OUT/'pexels-8059384.jpg').transpose(Image.Transpose.FLIP_LEFT_RIGHT).save(source)
call(op='init',settings=str(OUT/'mirror-settings'));call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'))
def wait():
 end=time.monotonic()+120
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError('timeout')
r=wait();assert r['count']==1
call(op='portrait_state',panel=3,select=0);wait();call(op='portrait_state',parameter='body.back',value=100);r=wait();p=r['recipe']['people'][0]
print('mirror',r['status'],flush=True)
(OUT/'mirror-recipe.json').write_text(json.dumps(r['recipe']),encoding='utf-8')
assert (p['settings'].get('parameters') or {}).get('body.back')==100,r['status']
size=np.array(Image.open(source).size);result=call(op='portrait_geometry',recipe=r['recipe'],size=size.tolist(),field=str(OUT/'mirror-field.bin'),source=str(source),render=str(OUT/'mirror-after.png'))
w,h=result['meshSize'];f=np.fromfile(OUT/'mirror-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
back=np.array([[1199-449,750],[1199-461,800],[1199-474,840]],float)/(size-1);shift=(forward_points(f,back)-back)*(size-1)
assert np.min(shift[:,0])<-1 and np.max(shift[:,0])<=0,shift
pose=np.array(p['settings']['pose']);fixed=np.concatenate([np.array(p['landmarks'])[:,:2],pose[[11,12,13,14,15,16,23,24,25,26],:2]])
error=float(np.linalg.norm((forward_points(f,fixed)-fixed)*(size-1),axis=1).max());assert error<.15,error
(OUT/'mirror-result.json').write_text(json.dumps({'sameSourceMirrored':True,'independentInference':True,'contourShift':shift.tolist(),'fixedMax':error},indent=2),encoding='utf-8');print(shift.tolist(),error,flush=True)
call(op='portrait_close');call(op='shutdown')
