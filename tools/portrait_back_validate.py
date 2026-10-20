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

import copy,base64,io,hashlib
OUT=ROOT/'docs/test-evidence/M3/semantic-back';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
size=np.array([640,800]);image=Image.new('RGBA',tuple(size),(35,39,42,255));draw=ImageDraw.Draw(image);mask=Image.new('L',tuple(size),0);md=ImageDraw.Draw(mask)
for y in range(100,501):
 t=(y-100)/400;rear=260-35*np.sin(np.pi*np.clip((t-.04)/.61,0,1))
 md.line((rear,y,380,y),fill=255);draw.line((rear,y,380,y),fill=(140+(y//12%2)*25,120,105,255))
 for x in [rear+5,300,340]:draw.point((round(x),y),fill='white')
source=OUT/'controlled-bend.png';image.save(source);buffer=io.BytesIO();mask.save(buffer,format='PNG')
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json').read_text(encoding='utf-8'));recipe['people']=recipe['people'][:1];recipe['sourceHash']=hashlib.sha256(image.tobytes()).hexdigest();p=recipe['people'][0];p['bodyMask']=base64.b64encode(buffer.getvalue()).decode();p['body']=[.3,.1,.4,.8];p['bodyAmbiguous']=False;p['settings']['parameters']={};pose=[[.5,.5,1] for i in range(33)]
for i,q in {0:[400,30],11:[310,100],12:[330,100],13:[430,220],14:[450,220],15:[470,330],16:[490,330],23:[310,500],24:[330,500],25:[310,680],26:[330,680],27:[310,780],28:[330,780]}.items():pose[i]=[q[0]/639,q[1]/799,1]
p['settings']['pose']=pose;p['settings']['automaticMasks']={}
for amount in [0,50,100]:
 p['settings']['parameters']={'body.back':amount};r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'field.bin'),render=str(OUT/f'back-{amount}.png'))
 if amount==0:assert r['identity'];continue
 w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
 points=np.array([[260-35*np.sin(np.pi*((y-100)/400-.04)/.61)+5,y] for y in [180,220,260]])/(size-1);output=forward_points(f,points)*(size-1)
 assert np.max(abs(output[:,1]-points[:,1]*(size[1]-1)))<.05
 assert np.min(output[:,0]-points[:,0]*(size[0]-1))>amount*.015,'posterior convexity not reduced'
 chest=np.array([[x,y] for y in [150,220,300] for x in [320,350,380]])/(size-1)
 assert np.max(abs((forward_points(f,chest)-chest)*(size-1)))<.05,'chest moved'
 anchors=np.array([pose[i][:2] for i in [11,12,13,14,15,16,23,24,25,26,27,28]]);assert np.linalg.norm((forward_points(f,anchors)-anchors)*(size-1),axis=1).max()<.1
 dx=np.gradient(f,1/(w-1),axis=1);dy=np.gradient(f,1/(h-1),axis=0);assert ((1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0]).min()>.5
# A detected object intersecting the posterior strip must not be retouched
# as anatomy; an unrelated object on the other side must not disable it.
for overlap in [True,False]:
 test=copy.deepcopy(recipe);objectMask=Image.new('L',tuple(size),0);d=ImageDraw.Draw(objectMask)
 d.rectangle((210,170,250,280) if overlap else (500,170,540,280),fill=255)
 data=io.BytesIO();objectMask.save(data,format='PNG');test['people'][0]['settings']['automaticMasks']['sceneOcclusion']=base64.b64encode(data.getvalue()).decode()
 if overlap:
  try:call(op='portrait_geometry',recipe=test,size=size.tolist());raise AssertionError('back object treated as torso')
  except RuntimeError as e:assert '物体' in str(e)
 else:assert not call(op='portrait_geometry',recipe=test,size=size.tolist())['identity']
# Long hair cannot become a fabricated inner back contour after subtracting
# hair from the body mask. Treat its overlap as occlusion, not exposed anatomy.
test=copy.deepcopy(recipe);hairMask=Image.new('L',tuple(size),0);ImageDraw.Draw(hairMask).rectangle((210,170,250,280),fill=255);data=io.BytesIO();hairMask.save(data,format='PNG');test['people'][0]['settings']['automaticMasks']['hair']=base64.b64encode(data.getvalue()).decode()
try:call(op='portrait_geometry',recipe=test,size=size.tolist());raise AssertionError('hidden back inferred under hair')
except RuntimeError as e:assert '头发' in str(e)
# Sitting, crossing arms and a straight torso cannot be blindly straightened.
for case in ['sitting','crossing','straight','frontal']:
 test=copy.deepcopy(recipe)
 if case=='sitting':
  for i,q in [(25,[150,510]),(26,[490,510])]:test['people'][0]['settings']['pose'][i]=[q[0]/639,q[1]/799,1]
 elif case=='crossing':test['people'][0]['settings']['pose'][15]=[.36,.3,1]
 elif case=='frontal':
  test['people'][0]['settings']['pose'][11]=[.2,.125,1];test['people'][0]['settings']['pose'][12]=[.8,.125,1]
 else:
  m=Image.new('L',tuple(size),0);ImageDraw.Draw(m).rectangle((260,100,380,500),fill=255);b=io.BytesIO();m.save(b,format='PNG');test['people'][0]['bodyMask']=base64.b64encode(b.getvalue()).decode()
 try:call(op='portrait_geometry',recipe=test,size=size.tolist(),source=str(source));raise AssertionError(case+' accepted')
 except RuntimeError as e:assert any(word in str(e) for word in ['坐姿','手臂','后凸','侧面']),str(e)
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
p['settings']['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=3,select=0);before=wait()['recipe'];call(op='portrait_state',parameter='body.back',value=80);after=wait();assert after['recipe']['people'][0]['settings']['parameters'].get('body.back')==80,after['status']
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'real.png'),save=str(OUT/'back.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after['recipe']);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'real.png')),np.array(Image.open(OUT/'reopened.png')));call(op='portrait_close');call(op='shutdown');print('Posterior upper-back convexity reduced, chest/head/joints fixed, no folds, sitting/arm/straight/frontal gates, UI and persistence passed')
