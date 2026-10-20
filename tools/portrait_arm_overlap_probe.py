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
OUT=ROOT/'docs/test-evidence/M3/semantic-arms';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
size=np.array([640,800]);source=OUT/'folded-arm.png';im=Image.new('RGBA',tuple(size),(30,35,40,255));mask=Image.new('L',tuple(size));d=ImageDraw.Draw(mask);d.line([(250,250),(250,550),(310,300)],fill=255,width=40);d.ellipse((285,255,340,315),fill=255);im.paste((160,120,95,255),mask=mask);im.save(source)
b=io.BytesIO();mask.save(b,format='PNG');recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json').read_text());p=recipe['people'][0];recipe['people']=[p];p['bodyMask']=base64.b64encode(b.getvalue()).decode();p['bodyAmbiguous']=False;p['settings']['automaticMasks']={};p['settings']['limbGeometryVersion']=2;p['settings']['parameters']={'body.arm.left':100};pose=[[.8,.8,1] for i in range(33)]
for i,(x,y) in {11:(250,250),13:(250,550),15:(310,300),17:(320,270),19:(325,265),21:(310,280)}.items():pose[i]=[x/639,y/799,1]
p['settings']['pose']=pose
try:
 r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),field=str(OUT/'folded-field.bin'),source=str(source),render=str(OUT/'folded-after.png'));w,h=r['meshSize'];f=np.fromfile(OUT/'folded-field.bin',np.float32).reshape(h,w,4)[:,:,:2];pts=np.array([pose[i][:2] for i in [11,13,15,17,19,21]]);print('joint/hand movement',((forward_points(f,pts)-pts)*(size-1)).tolist())
except RuntimeError as e:
 assert '手臂轮廓' in str(e),str(e)
 print('refused',e)
else:raise AssertionError('merged arm contour accepted')
(OUT/'folded-recipe.json').write_text(json.dumps(recipe),encoding='utf-8')
recipe['sourceHash']=hashlib.sha256(im.tobytes()).hexdigest();p['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=recipe)
def wait():
 for i in range(200):
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError('preview did not settle')
wait();call(op='portrait_state',panel=3,select=0,linked=False,side=0);before=wait()['recipe'];call(op='portrait_state',parameter='body.arm',value=100);after=wait()
assert after['recipe']==before,'rejected arm edit mutated settings'
assert '手臂轮廓' in after['status'],after['status']
call(op='portrait_state',incomingPreset={'limbGeometryVersion':2,'parameters':{'body.arm.left':100}})
call(op='portrait_state',panel=15)
before=wait()['recipe']
call(op='portrait_state',presetIndex=5)
call(op='portrait_state',click='portraitPreviewPreset')
after=wait()
assert after['recipe']==before,'rejected arm preset mutated settings'
assert '手臂轮廓' in after['status'],after['status']
call(op='portrait_close');call(op='shutdown');print('ambiguous arm rejected before committing parameters')
