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




import io,base64,math
for name,side in [('leroy-chiao',0),('leroy-chiao',1),('christina-koch',0),('christina-koch',1),('jasmin-moghbeli',1)]:
 source=ROOT/'docs/test-evidence/M3/quality'/f'{name}-source.png';size=np.array(Image.open(source).size)
 recipe=json.loads((OUT/f'{name}-production-recipe.json').read_text());p=recipe['people'][0];pose=np.array(p['settings']['pose'])[:,:2]*(size-1);lm=np.array(p['landmarks'])[:,:2];center=pose[[11,12]].mean(0);width=np.linalg.norm(pose[11]-pose[12]);axis=(pose[11]-pose[12])*(1 if side==0 else -1)/width;down=np.array([-axis[1],axis[0]]);down*=1 if down[1]>=0 else -1
 body=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['bodyMask']))).convert('L'));hair=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['settings']['automaticMasks']['hair']))).convert('L'))
 def mask(a,q):
  x,y=(q/(size-1)*np.array(a.shape[::-1])).astype(int)
  return int(a[y,x]) if 0<=x<a.shape[1] and 0<=y<a.shape[0] else 0
 step=max(.5,width/300);top=max(-width*.5,np.dot(lm[8]*(size-1)-center,down)-width*.08)
 def trace(x):
  for y in np.arange(top,width*.35+step*.1,step):
   q=center+axis*x+down*y
   if mask(body,q)>180:return None if y<=top+step or mask(hair,q)>100 else y
  return None
 runs=[];run=[]
 for i,x in enumerate(np.linspace(.12,.75,65)):
  if trace(x*width) is None:
   if run:runs.append(run);run=[]
  else:run.append(x)
 if run:runs.append(run)
 run=max(runs,key=len);assert len(run)>=17
 t=np.linspace(0,1,25);x=np.linspace(run[0],run[-1],25)*width;y=np.array([trace(v) for v in x]);points=center+x[:,None]*axis+y[:,None]*down
 p['settings']['parameters']={'body.squareShoulder.'+('left' if side==0 else 'right'):100}
 r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'square-field.bin'),render=str(OUT/f'{name}-square-final-{side}.png'))
 w,h=r['meshSize'];f=np.fromfile(OUT/'square-field.bin',np.float32).reshape(h,w,4)[:,:,:2];after=forward_points(f,points/(size-1))*(size-1);select=(t>=.65)&(t<=.9)
 initial=float(np.polyfit(x[select],y[select],1)[0]);final=float(np.polyfit(x[select],((after-center)@down)[select],1)[0])
 head=float(np.linalg.norm((forward_points(f,lm)-lm)*(size-1),axis=1).max())
 yy,xx=np.nonzero(hair>220);hp=np.stack([xx[::5],yy[::5]],axis=1)/np.array(hair.shape[::-1]);hairDrift=float(np.linalg.norm((forward_points(f,hp)-hp)*(size-1),axis=1).max())
 record=dict(name=name,side=side,initialSlope=initial,finalSlope=final,headDrift=head,hairDrift=hairDrift);records.append(record);print(record,flush=True)
 overlay=Image.open(source).convert('RGB');d=ImageDraw.Draw(overlay);d.line([tuple(q) for q in points],fill='red',width=3);d.line([tuple(q) for q in after],fill='cyan',width=3);overlay.save(OUT/f'{name}-square-curve-{side}.png')
(OUT/'square-real-results.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
assert all(abs(r['finalSlope'])<=abs(r['initialSlope'])*.6+.05 and r['headDrift']<.5 and r['hairDrift']<.5 for r in records)
