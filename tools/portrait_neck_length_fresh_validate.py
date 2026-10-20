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


OUT=ROOT/'docs/test-evidence/M3/semantic-neck';call(op='init',settings=str(OUT/'length-real-settings'));records=[]
for name in ['leroy-chiao','christina-koch','jasmin-moghbeli']:
 source=ROOT/'docs/test-evidence/M3/quality'/f'{name}-source.png';size=np.array(Image.open(source).size)
 recipePath=ROOT/'docs/test-evidence/M3/semantic-shoulder'/f'{name}-production-recipe.json'
 recipe=json.loads(recipePath.read_text());p=recipe['people'][0]
 for amount in [-100,100]:
  p['settings']['parameters']={'body.neckLength':amount}
  try:r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'length-real-field.bin'),render=str(OUT/f'{name}-length-fresh-{amount}.png'))
  except RuntimeError as e:
   records.append(dict(name=name,amount=amount,refused=str(e)));continue
  w,h=r['meshSize'];f=np.fromfile(OUT/'length-real-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  lm=np.array(p['landmarks'])[:,:2];pose=np.array(p['settings']['pose'])[:,:2]
  down=(lm[8]-lm[36:48].mean(0))*(size-1)
  left,right=pose[[11,12]]*(size-1);chin=lm[8]*(size-1)
  distance=np.linalg.solve(np.column_stack([down,left-right]),left-chin)[0]
  headMotion=(forward_points(f,lm)-lm)*(size-1)
  gain=np.median(headMotion,axis=0)
  assert np.dot(gain,down)*amount<0, "neck length moved head in wrong direction"
  assert abs(np.linalg.det(np.stack([gain,down])))/np.linalg.norm(down)<.1, "neck length moved head sideways"
  assert np.linalg.norm(gain)>.2, "neck length has no visible effect"
  error=float(np.linalg.norm((forward_points(f,lm)-lm)*(size-1)-gain,axis=1).max())
  fixed=pose[[11,12]];drift=float(np.linalg.norm((forward_points(f,fixed)-fixed)*(size-1),axis=1).max())
  records.append(dict(name=name,amount=amount,headError=error,shoulderDrift=drift))
(OUT/'length-fresh-real.json').write_text(json.dumps(records,ensure_ascii=False,indent=2));print(records);call(op='shutdown')
assert len(records)==6 and all('headError' in r for r in records),'real-photo coverage incomplete'
assert all(r.get('headError',0)<.5 and r.get('shoulderDrift',0)<.5 for r in records),'neck/head/clavicle preservation incomplete'
