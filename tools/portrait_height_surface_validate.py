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

region=Image.new('L',tuple(size));mark=ImageDraw.Draw(region)
mark.polygon([(515,1267),(538,1245),(548,1210),(576,1215),(585,1268),(575,1277),(540,1276)],fill=255)
mark.polygon([(591,1273),(594,1230),(620,1208),(640,1215),(641,1244),(666,1268),(659,1279),(617,1279)],fill=255)
ys,xs=np.nonzero(np.asarray(region));feet=np.stack([xs[::2],ys[::2]],axis=1)/(size-1)
import base64,io
hair=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['settings']['automaticMasks']['hair']))).convert('L'))
hy,hx=np.nonzero(hair>220);hairpoints=np.stack([hx[::4],hy[::4]],axis=1)/np.array(hair.shape[::-1])
pose=np.array(p['settings']['pose']);gain=(pose[[11,12],:2].mean(0)-pose[[27,28],:2].mean(0))*(size-1)*.05
records=[]
for amount in [-100,50,100]:
 p['settings']['parameters']={'body.height':amount}
 r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'surface-field.bin'),render=str(OUT/f'surface-{amount}.png'))
 w,h=r['meshSize'];f=np.fromfile(OUT/'surface-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
 drift=float(np.linalg.norm((forward_points(f,feet)-feet)*(size-1),axis=1).max())
 print(amount,'foot drift',drift,flush=True);assert drift<.5,(amount,'foot shape',drift)
 hairError=float(np.linalg.norm((forward_points(f,hairpoints)-hairpoints)*(size-1)-gain*amount*.01,axis=1).max())
 print(amount,'hair translation deviation',hairError,flush=True)
 assert hairError<.2,(amount,'hairstyle distorted',hairError)
 records.append(dict(amount=amount,footDrift=drift,hairTranslationDeviation=hairError))
(OUT/'surface-metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
