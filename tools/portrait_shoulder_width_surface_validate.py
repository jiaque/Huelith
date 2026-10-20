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



import io,base64
for name in ['leroy-chiao','jasmin-moghbeli','christina-koch']:
 source=ROOT/'docs/test-evidence/M3/quality'/f'{name}-source.png';size=np.array(Image.open(source).size)
 recipePath=OUT/f'{name}-production-recipe.json'
 recipe=json.loads(recipePath.read_text());p=recipe['people'][0];pose=np.array(p['settings']['pose'])[:,:2]*(size-1);span=pose[12]-pose[11];width=np.linalg.norm(span);axis=span/width;vertical=np.array([-axis[1],axis[0]])
 hair=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['settings']['automaticMasks']['hair']))).convert('L'));y,x=np.nonzero(hair>220);hairProbes=np.stack([x[::5],y[::5]],axis=1)/np.array(hair.shape[::-1])
 for amount in [-100,100]:
  p['settings']['parameters']={'body.shoulderWidth.left':amount,'body.shoulderWidth.right':amount}
  try:r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'surface-field.bin'),render=str(OUT/f'{name}-production-surface-{amount}.png'))
  except RuntimeError as e:
   records.append(dict(name=name,amount=amount,refused=str(e)));print(records[-1],flush=True);continue
  w,h=r['meshSize'];f=np.fromfile(OUT/'surface-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  hairError=float(np.linalg.norm((forward_points(f,hairProbes)-hairProbes)*(size-1),axis=1).max())
  errors=[]
  for side in [0,1]:
   points=np.array([pose[11+side]+axis*x*width+vertical*y*width for x in [-.04,0,.04] for y in [-.04,0,.04]])/(size-1)
   expected=span*(-1 if side==0 else 1)*.06*amount/100
   errors.append(float(np.linalg.norm((forward_points(f,points)-points)*(size-1)-expected,axis=1).max()))
  armErrors=[]
  for side in [0,1]:
   shift=span*(-1 if side==0 else 1)*.06*amount/100
   for segment in [0,1]:
    first,last=pose[11+side+segment*2],pose[13+side+segment*2];v=last-first;normal=np.array([-v[1],v[0]])/np.linalg.norm(v)
    for t in [.25,.5,.75,1]:
     points=np.array([first+v*t+normal*width*c for c in [-.025,0,.025]])/(size-1)
     smooth=t*t*(3-2*t);weight=1-.5*smooth if segment==0 else .5*(1-smooth)
     armErrors.append(float(np.linalg.norm((forward_points(f,points)-points)*(size-1)-shift*weight,axis=1).max()))
  records.append(dict(name=name,amount=amount,hairError=hairError,capError=max(errors),armError=max(armErrors)));print(records[-1],flush=True)
(OUT/'production-surface.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
assert len(records)==6 and all('refused' not in r and r['hairError']<.5 and r['capError']<.5 and r['armError']<.5 for r in records)
