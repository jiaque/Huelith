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


OUT=ROOT/'docs/test-evidence/M3/semantic-shoulder';call(op='init',settings=str(OUT/'length-real-settings'));records=[]


for name in ['jasmin-moghbeli','christina-koch','leroy-chiao','jessica-watkins']:
 source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png'
 call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'))
 end=time.monotonic()+120
 while time.monotonic()<end:
  call(op='pump',waitMs=20);state=call(op='portrait_state')
  if state['ready'] and not state['previewBusy']:break
 else:raise AssertionError('recognition timeout')
 recipe=state['recipe'];call(op='portrait_close');(OUT/f'{name}-production-recipe.json').write_text(json.dumps(recipe))
 p=recipe['people'][0];pose=p['settings'].get('pose',[]);print(name,'pose points',len(pose),flush=True)
 image=Image.open(source).convert('RGB');draw=ImageDraw.Draw(image);size=np.array(image.size)
 for i in [0,11,12,13,14,15,16]:
  if len(pose)!=33:break
  x,y=np.array(pose[i])[:2]*size;draw.ellipse((x-4,y-4,x+4,y+4),fill='red');draw.text((x,y),str(i),fill='white')
 image.save(OUT/f'{name}-production-pose.png')
 assert p['settings']['poseDetectionVersion']==2
 if name!='jessica-watkins':assert len(pose)==33
 records.append(dict(name=name,points=len(pose)))
(OUT/'production-pose-results.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
