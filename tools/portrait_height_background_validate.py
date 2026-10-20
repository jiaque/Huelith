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

OUT=ROOT/'docs/test-evidence/M3/semantic-height';call(op='init',settings=str(OUT/'real-settings'));source=ROOT/'docs/test-evidence/M3/semantic-back/mr-yoga-side-bend.jpg';recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-back/mr-yoga-recipe.json').read_text(encoding='utf-8'));size=np.array(Image.open(source).size);p=recipe['people'][0];p['settings']['parameters']={'body.height':100};p['settings']['heightVersion']=2


records=[]
for x,y0,y1 in [(300,750,1150),(250,750,1100),(850,600,1100)]:
 p['settings']['backgroundLines']=[[x/size[0],y0/size[1],x/size[0],y1/size[1]]]
 for amount in [-100,100]:
  p['settings']['parameters']={'body.height':amount}
  r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),field=str(OUT/'background-field.bin'))
  w,h=r['meshSize'];f=np.fromfile(OUT/'background-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  pts=np.array([[x,y] for y in np.linspace(y0,y1,60)])/(size-1)
  drift=float(np.linalg.norm((forward_points(f,pts)-pts)*(size-1),axis=1).max())
  print(x,amount,drift,flush=True);assert drift<.1
  records.append(dict(x=x,amount=amount,lineDrift=drift))
(OUT/'background-metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
