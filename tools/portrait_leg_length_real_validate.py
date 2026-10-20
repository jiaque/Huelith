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

OUT=ROOT/'docs/test-evidence/M3/semantic-leg-length';call(op='init',settings=str(OUT/'real-settings'));records=[]
cases=[(ROOT/f'docs/test-evidence/M3/quality/{n}-recipe.json',ROOT/f'docs/test-evidence/M3/quality/{n}-source.png') for n in ['leroy-chiao','christina-koch','jasmin-moghbeli','FudanPed00001','jessica-watkins']]
cases += [(ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json',ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg'),(ROOT/'docs/test-evidence/M3/semantic-back/mr-yoga-recipe.json',ROOT/'docs/test-evidence/M3/semantic-back/mr-yoga-side-bend.jpg')]
for recipePath,source in cases:
 recipe=json.loads(recipePath.read_text(encoding='utf-8'));size=np.array(Image.open(source).size)
 for index,p in enumerate(recipe['people']):
  for item in recipe['people']:item['settings']['parameters']={};item['settings']['legLengthVersion']=2
  for side in range(2):
   p['settings']['parameters']={'body.legLength.'+['left','right'][side]:100}
   item={'source':source.name,'person':index,'side':side}
   try:
    r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'real-field.bin'),render=str(OUT/f'{source.stem}-{index}-{side}.png'))
    w,h=r['meshSize'];f=np.fromfile(OUT/'real-field.bin',np.float32).reshape(h,w,4)[:,:,:2];pose=np.array(p['settings']['pose']);pts=pose[[23+side,25+side,27+side,29+side,31+side],:2];shift=(forward_points(f,pts)-pts)*(size-1);assert np.linalg.norm(shift[0])<.5
    hip,knee,ankle=pts[:3]*(size-1);delta=ankle-hip;expected=delta*.06
    assert np.max(np.linalg.norm(shift[2:]-expected,axis=1))<.5,'foot deformed'
    item['kneeShift']=shift[1].tolist();item['footShift']=shift[2].tolist();item['footRigidError']=float(np.linalg.norm(shift[2:]-expected,axis=1).max());dx=np.gradient(f,1/(w-1),axis=1);dy=np.gradient(f,1/(h-1),axis=0);item['minimumJacobian']=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0]).min());assert item['minimumJacobian']>.1
   except RuntimeError as e:item['refused']=str(e)
   records.append(item);print(item,flush=True)
assert any('kneeShift' in item for item in records),'no successful real-photo case'
(OUT/'real-metrics.json').write_text(json.dumps(records,ensure_ascii=False,indent=2));call(op='shutdown')
