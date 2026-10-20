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
OUT=ROOT/'docs/test-evidence/M3/semantic-leg-length';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
size=np.array([600,800]);image=Image.new('RGBA',tuple(size),(38,41,45,255));mask=Image.new('L',tuple(size),0);draw=ImageDraw.Draw(image);md=ImageDraw.Draw(mask)
for points in [[(180,250),(200,480),(180,720)],[(420,250),(400,480),(420,720)]]:
 md.line(points,fill=255,width=44,joint='curve');draw.line(points,fill=(165,135,110,255),width=44,joint='curve')
 for dx in [-12,0,12]:draw.line([(x+dx,y) for x,y in points],fill='white',width=2)
source=OUT/'legs.png';image.save(source);buffer=io.BytesIO();mask.save(buffer,format='PNG')
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json').read_text(encoding='utf-8'));recipe['people']=recipe['people'][:1];recipe['sourceHash']=hashlib.sha256(image.tobytes()).hexdigest();p=recipe['people'][0];p['bodyMask']=base64.b64encode(buffer.getvalue()).decode();p['bodyAmbiguous']=False;p['settings']['parameters']={};p['settings']['automaticMasks']={};pose=[[.5,.5,1] for i in range(33)]
for i,q in {11:(220,100),12:(380,100),23:(180,250),24:(420,250),25:(200,480),26:(400,480),27:(180,720),28:(420,720),29:(170,730),30:(430,730),31:(150,735),32:(450,735)}.items():pose[i]=[q[0]/599,q[1]/799,1]
p['settings']['pose']=pose;p['settings']['legLengthVersion']=2;records=[]
for amount in [-100,0,50,100]:
 p['settings']['parameters']={'body.legLength.left':amount};r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'field.bin'),render=str(OUT/f'left-{amount}.png'))
 if amount==0:assert r['identity'];continue
 w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
 points=np.array([[188,480],[200,480],[212,480],[180,250],[180,720],[170,730],[150,735],[400,480]])/(size-1);out=forward_points(f,points)*(size-1);shift=out-points*(size-1)
 assert abs(shift[1,1]-(230/470)*470*.06*amount/100)<.15
 assert abs((out[2,0]-out[0,0])-24)<.2,'knee width changed';assert np.linalg.norm(shift[[3,7]],axis=1).max()<.1,'hip/opposite leg drift'
 assert np.max(abs(shift[4:7]-[0,470*.06*amount/100]))<.15,'foot must translate rigidly'
 dx=np.gradient(f,1/(w-1),axis=1);dy=np.gradient(f,1/(h-1),axis=0);jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0]).min());assert jac>.25
 records.append({'amount':amount,'kneeShift':shift[1].tolist(),'footShift':shift[4].tolist(),'minimumJacobian':jac})
combined=copy.deepcopy(recipe);combined['people'][0]['settings']['parameters']={'body.legLength.left':100,'body.straightLeg.left':100};call(op='portrait_geometry',recipe=combined,size=size.tolist(),source=str(source),render=str(OUT/'combined.png'))
for case in ['crossed','bent','missing','missingFoot','canvas']:
 r=copy.deepcopy(recipe);q=r['people'][0]['settings']['pose']
 if case=='crossed':q[26]=[200/599,480/799,1]
 if case=='bent':q[25]=[350/599,480/799,1]
 if case=='missing':q[27][2]=.1
 if case=='missingFoot':q[31][2]=.1
 if case=='canvas':q[31]=[150/599,795/799,1]
 try:call(op='portrait_geometry',recipe=r,size=size.tolist());raise AssertionError(case+' accepted')
 except RuntimeError:pass

def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
portable=call(op='portrait_geometry',portableSkin=p['settings'],bodyPreset=True,targetSettings={})
assert portable['portable']['legLengthVersion']==2 and portable['merged']['legLengthVersion']==2
p['settings']['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=3,select=0);before=wait()['recipe'];call(op='portrait_state',parameter='body.legLength',value=80);after=wait();assert after['recipe']['people'][0]['settings']['parameters'].get('body.legLength.left')==80,after['status']
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'window.png'),save=str(OUT/'legs.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after['recipe']);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'window.png')),np.array(Image.open(OUT/'reopened.png')));call(op='portrait_close');call(op='shutdown');(OUT/'metrics.json').write_text(json.dumps(records,indent=2));print(records);print('Segmented leg length, rigid knee/foot translation, fixed hip/opposite leg, rejection and UI persistence passed')
