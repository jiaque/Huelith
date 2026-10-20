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

import io,base64,hashlib,copy
OUT=ROOT/'docs/test-evidence/M3/semantic-shoulder';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
size=np.array([640,900]);image=Image.new('RGBA',tuple(size),(50,60,70,255));source=OUT/'controlled.png';image.save(source)
mask=Image.new('L',tuple(size),255);buffer=io.BytesIO();mask.save(buffer,format='PNG')
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json').read_text(encoding='utf-8'));recipe['people']=recipe['people'][:1];recipe['sourceHash']=hashlib.sha256(image.tobytes()).hexdigest();p=recipe['people'][0];p['bodyMask']=base64.b64encode(buffer.getvalue()).decode();p['bodyAmbiguous']=False;p['face']=[280/639,100/899,80/639,90/899];p['body']=[.15,.05,.7,.9];p['landmarks']=[[(285+(i%6)*13)/639,(105+(i//6)*7)/899,1] for i in range(66)];p['landmarks'][8]=[320/639,190/899,1];p['settings']['parameters']={};p['settings']['neckGeometryVersion']=2
pose=[[.5,.5,1] for i in range(33)]
for i,q in {11:(250,250),12:(390,250),23:(270,450),24:(370,450)}.items():pose[i]=[q[0]/639,q[1]/899,1]
p['settings']['pose']=pose
for i in range(36,48):p['landmarks'][i]=[(300+(i-36)*40/11)/639,140/899,1]
skin=Image.new('L',tuple(size));ImageDraw.Draw(skin).rectangle((280,190,360,250),fill=255);buffer=io.BytesIO();skin.save(buffer,format='PNG');p['settings']['automaticMasks']={'skin':base64.b64encode(buffer.getvalue()).decode()}

for i,q in {13:(220,350),14:(420,350),15:(210,440),16:(430,440)}.items():pose[i]=[q[0]/639,q[1]/899,1]
p['settings']['pose']=pose;p['settings']['backgroundLines']=[]

body=Image.new('L',tuple(size));ImageDraw.Draw(body).polygon([(300,180),(340,180),(348,220),(390,250),(440,450),(200,450),(250,250),(292,220)],fill=255)
buffer=io.BytesIO();body.save(buffer,format='PNG');p['bodyMask']=base64.b64encode(buffer.getvalue()).decode()
p['settings']['backgroundLines']=[]
p['settings']['trapeziusGeometryVersion']=2
records=[]
for amount in [-100,50,100]:
 p['settings']['parameters']={'body.trapezius.left':amount}
 r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),field=str(OUT/'trapezius-field.bin'))
 w,h=r['meshSize'];f=np.fromfile(OUT/'trapezius-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
 probes=np.array([[271,235],[320,220],[320,190],[390,250],[250,250],[220,350],[420,350]])/(size-1)
 delta=(forward_points(f,probes)-probes)*(size-1)
 assert delta[0,1]*amount>0 and abs(delta[0,1])>abs(amount)*.025,delta
 assert abs(delta[0,0])<.1 and np.linalg.norm(delta[1:],axis=1).max()<.3,delta
 records.append(dict(amount=amount,inset=delta[0].tolist(),fixedDrift=float(np.linalg.norm(delta[1:],axis=1).max())))
 print(records[-1],flush=True)
(OUT/'trapezius-controlled.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
