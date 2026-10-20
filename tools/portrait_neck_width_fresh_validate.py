"""Exercise production inverse fields and inspect real portrait crops offscreen."""
import os, ctypes, json, time
from pathlib import Path
import numpy as np
import io,base64
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


for name in ['leroy-chiao','jasmin-moghbeli','christina-koch']:
 source=ROOT/'docs/test-evidence/M3/quality'/f'{name}-source.png';size=np.array(Image.open(source).size)
 recipePath=ROOT/'docs/test-evidence/M3/semantic-shoulder'/f'{name}-production-recipe.json'
 recipe=json.loads(recipePath.read_text());p=recipe['people'][0];p['settings']['neckGeometryVersion']=2
 lm=np.array(p['landmarks'])[:,:2];pose=np.array(p['settings']['pose'])[:,:2]
 down=(lm[8]-lm[36:48].mean(0))*(size-1);down/=np.linalg.norm(down)
 for amount in [-100,100]:
  p['settings']['parameters']={'body.neck':amount}
  r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'width-field.bin'),render=str(OUT/f'{name}-width-fresh-{amount}.png'))
  w,h=r['meshSize'];f=np.fromfile(OUT/'width-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  displacement=f*(size-1)
  axial=float(np.abs(displacement@down).max());motion=float(np.linalg.norm(displacement,axis=2).max())
  head=float(np.linalg.norm((forward_points(f,lm)-lm)*(size-1),axis=1).max())
  hair=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['settings']['automaticMasks']['hair']))).convert('L'))
  yy,xx=np.nonzero(hair>220);hp=np.stack([xx[::3],yy[::3]],axis=1)/np.array(hair.shape[::-1])
  hairDrift=float(np.linalg.norm((forward_points(f,hp)-hp)*(size-1),axis=1).max())
  shoulders=pose[[11,12]]
  shoulderDrift=float(np.linalg.norm((forward_points(f,shoulders)-shoulders)*(size-1),axis=1).max())
  dy,dx=np.gradient(f,1/(h-1),1/(w-1),axis=(0,1))
  minimumJacobian=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min())
  records.append(dict(name=name,amount=amount,axialMotion=axial,maximumMotion=motion,headDrift=head,hairDrift=hairDrift,shoulderDrift=shoulderDrift,minimumJacobian=minimumJacobian));print(records[-1],flush=True)
  assert axial<.01 and motion>1 and head<.5 and hairDrift<.5 and shoulderDrift<.5 and minimumJacobian>.1
(OUT/'width-fresh-real.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
