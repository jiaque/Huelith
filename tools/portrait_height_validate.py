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
OUT=ROOT/'docs/test-evidence/M3/semantic-height';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
size=np.array([640,900]);image=Image.new('RGBA',tuple(size),(35,40,45,255));draw=ImageDraw.Draw(image)
for y in range(0,900,20):draw.line((0,y,639,y),fill=(65,70,75,255))
source=OUT/'controlled.png';image.save(source);mask=Image.new('L',tuple(size),255);buffer=io.BytesIO();mask.save(buffer,format='PNG')
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json').read_text(encoding='utf-8'));recipe['people']=recipe['people'][:1];recipe['sourceHash']=hashlib.sha256(image.tobytes()).hexdigest();p=recipe['people'][0];p['bodyMask']=base64.b64encode(buffer.getvalue()).decode();p['bodyAmbiguous']=False;p['face']=[280/639,100/899,80/639,90/899];p['body']=[.15,.05,.7,.9];p['landmarks']=[[ (285+(i%6)*13)/639,(105+(i//6)*7)/899,1] for i in range(66)];p['settings']['parameters']={};p['settings']['automaticMasks']={};p['settings']['heightVersion']=2;pose=[[.5,.5,1] for i in range(33)]
for i,q in {11:(250,250),12:(390,250),13:(150,350),14:(490,350),15:(120,420),16:(520,420),23:(270,450),24:(370,450),25:(260,625),26:(380,625),27:(250,800),28:(390,800),29:(240,810),30:(400,810),31:(220,815),32:(420,815)}.items():pose[i]=[q[0]/639,q[1]/899,1]
p['settings']['pose']=pose;records=[]
for amount in [-100,0,50,100]:
 p['settings']['parameters']={'body.height':amount};r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'field.bin'),render=str(OUT/f'height-{amount}.png'))
 if amount==0:assert r['identity'];continue
 w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
 indices=[11,12,13,14,15,16,23,24,25,26,27,28,29,30,31,32];pts=np.array([pose[i][:2] for i in indices]);shift=(forward_points(f,pts)-pts)*(size-1);gain=-27.5*amount/100
 expected=np.array([[0,gain*k] for k in [1,1,1,1,1,1,.75,.75,.375,.375,0,0,0,0,0,0]])
 error=float(np.linalg.norm(shift-expected,axis=1).max());assert error<.5,error
 face=np.array(p['landmarks'])[:,:2];faceShift=(forward_points(f,face)-face)*(size-1);headError=float(np.linalg.norm(faceShift-[0,gain],axis=1).max());assert headError<.5,headError
 dx=np.gradient(f,1/(w-1),axis=1);dy=np.gradient(f,1/(h-1),axis=0);jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0]).min());assert jac>.1
 records.append({'amount':amount,'jointError':error,'headError':headError,'minimumJacobian':jac,'milliseconds':r['milliseconds']})
for case in ['sitting','missing','cropped']:
 test=copy.deepcopy(recipe);q=test['people'][0]['settings']['pose']
 if case=='sitting':q[25]=[.1,.52,1]
 if case=='missing':q[31][2]=.1
 if case=='cropped':test['people'][0]['face']=[.4,.01,.1,.1]
 try:call(op='portrait_geometry',recipe=test,size=size.tolist());raise AssertionError(case+' accepted')
 except RuntimeError:pass
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(records)
