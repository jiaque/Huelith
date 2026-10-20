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

for name,folder,recipeName,filename in [('standing','semantic-back','mr-yoga-recipe.json','mr-yoga-side-bend.jpg'),('walking','quality','FudanPed00001-recipe.json','FudanPed00001-source.png')]:
 source=ROOT/'docs/test-evidence/M3'/folder/filename;size=np.array(Image.open(source).size)
 recipe=json.loads((source.parent/recipeName).read_text())
 for p in recipe['people']:p['settings']['parameters']={};p['settings']['legLengthVersion']=2
 p=recipe['people'][0];pose=np.array(p['settings']['pose']);hips=pose[[23,24],:2];ankles=pose[[27,28],:2]
 axis=(ankles.mean(0)-hips.mean(0))*(size-1)
 for amount in [-100,50,100]:
  p['settings']['parameters']={'body.legLength.left':amount,'body.legLength.right':amount}
  r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'paired-field.bin'),render=str(OUT/f'{name}-paired-{amount}.png'))
  w,h=r['meshSize'];f=np.fromfile(OUT/'paired-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  region=Image.new('L',tuple(size));mark=ImageDraw.Draw(region)
  if name=='walking':
   mark.polygon([(158,399),(180,402),(190,396),(199,409),(191,423),(179,421),(166,410)],fill=255)
   mark.polygon([(259,423),(277,418),(289,413),(299,417),(302,429),(284,433),(263,431)],fill=255)
  else:
   mark.polygon([(515,1267),(538,1245),(548,1210),(576,1215),(585,1268),(575,1277),(540,1276)],fill=255)
   mark.polygon([(591,1273),(594,1230),(620,1208),(640,1215),(641,1244),(666,1268),(659,1279),(617,1279)],fill=255)
  ys,xs=np.nonzero(np.asarray(region));feet=np.stack([xs[::2],ys[::2]],axis=1)/(size-1)
  shift=(forward_points(f,feet)-feet)*(size-1);expected=axis*.06*amount*.01
  error=float(np.linalg.norm(shift-expected,axis=1).max());assert error<.5,(name,amount,'feet deformed',error)
  fixed=pose[[0,11,12,23,24],:2];drift=float(np.linalg.norm((forward_points(f,fixed)-fixed)*(size-1),axis=1).max());assert drift<.1,(name,amount,'torso moved',drift)
  records.append(dict(name=name,amount=amount,footError=error,expected=expected.tolist(),fixedDrift=drift))
(OUT/'paired-metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(records)
