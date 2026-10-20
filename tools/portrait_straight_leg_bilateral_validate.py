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
OUT=ROOT/'docs/test-evidence/M3/semantic-straight-leg';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
size=np.array([600,800]);image=Image.new('RGBA',tuple(size),(38,41,45,255));mask=Image.new('L',tuple(size),0);draw=ImageDraw.Draw(image);md=ImageDraw.Draw(mask)
for points in [[(180,250),(200,480),(180,720)],[(420,250),(400,480),(420,720)]]:
 md.line(points,fill=255,width=44,joint='curve');draw.line(points,fill=(165,135,110,255),width=44,joint='curve')
 for dx in [-12,0,12]:draw.line([(x+dx,y) for x,y in points],fill='white',width=2)
source=OUT/'legs.png';image.save(source);buffer=io.BytesIO();mask.save(buffer,format='PNG')
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json').read_text(encoding='utf-8'));recipe['people']=recipe['people'][:1];recipe['sourceHash']=hashlib.sha256(image.tobytes()).hexdigest();p=recipe['people'][0];p['bodyMask']=base64.b64encode(buffer.getvalue()).decode();p['bodyAmbiguous']=False;p['settings']['parameters']={};p['settings']['automaticMasks']={};pose=[[.5,.5,1] for i in range(33)]
for i,q in {11:(220,100),12:(380,100),23:(180,250),24:(420,250),25:(200,480),26:(400,480),27:(180,720),28:(420,720)}.items():pose[i]=[q[0]/599,q[1]/799,1]
p['settings']['pose']=pose;records=[]

p['settings'].pop('backgroundLines',None)

for separation in [44,60,100]:
 mask=Image.new('L',tuple(size));md=ImageDraw.Draw(mask)
 chains=[[(180,250),(200,480),(180,720)],[(220+separation,250),(200+separation,480),(220+separation,720)]]
 for chain in chains:md.line(chain,fill=255,width=44,joint='curve')
 buffer=io.BytesIO();mask.save(buffer,format='PNG');p['bodyMask']=base64.b64encode(buffer.getvalue()).decode()
 for i,q in zip([24,26,28],chains[1]):pose[i]=[q[0]/599,q[1]/799,1]
 p['settings']['pose']=pose
 for amount in [25,50,100]:
  p['settings']['parameters']={'body.straightLeg.left':amount,'body.straightLeg.right':amount}
  r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),field=str(OUT/'bilateral-field.bin'))
  w,h=r['meshSize'];f=np.fromfile(OUT/'bilateral-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  points=np.array([[194,480],[200,480],[206,480],[194+separation,480],[200+separation,480],[206+separation,480]])/(size-1)
  moved=forward_points(f,points)*(size-1)
  assert moved[1,0]<200-amount*.03 and moved[4,0]>200+separation+amount*.03
  assert abs(moved[2,0]-moved[0,0]-12)<.3
  assert abs(moved[5,0]-moved[3,0]-12)<.3
  edges=np.array([[222,480],[178+separation,480]])/(size-1)
  edges=forward_points(f,edges)*(size-1);assert edges[1,0]>=edges[0,0]-.05
  dx=np.gradient(f,1/(w-1),axis=1);dy=np.gradient(f,1/(h-1),axis=0)
  jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0]).min());assert jac>.25
  records.append(dict(case='controlled',separation=separation,amount=amount,minimumJacobian=jac))
# Independently marked image rectangles contain visible feet/shoes and ground.
for name,folder,filename,rect in [('walking','quality','FudanPed00001-source.png',(150,390,305,438)),('standing','semantic-back','mr-yoga-side-bend.jpg',(510,1210,680,1290))]:
 source=ROOT/'docs/test-evidence/M3'/folder/filename
 recipeName='FudanPed00001-recipe.json' if name=='walking' else 'mr-yoga-recipe.json'
 recipe=json.loads((source.parent/recipeName).read_text());size=np.array(Image.open(source).size)
 for p in recipe['people']:p['settings']['parameters']={}
 call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),render=str(OUT/(name+'-baseline.png')))
 baseline=np.asarray(Image.open(OUT/(name+'-baseline.png')))
 x0,y0,x1,y1=rect
 region=Image.new('L',tuple(size));mark=ImageDraw.Draw(region)
 if name=='walking':
  mark.polygon([(158,399),(180,402),(190,396),(199,409),(191,423),(179,421),(166,410)],fill=255)
  mark.polygon([(259,423),(277,418),(289,413),(299,417),(302,429),(284,433),(263,431)],fill=255)
 else:mark.rectangle(rect,fill=255)
 region=np.asarray(region)>0
 ys,xs=np.nonzero(region);probes=np.stack([xs[::3],ys[::3]],axis=1)/(size-1)
 for amount in [25,50,100]:
  recipe['people'][0]['settings']['parameters']={'body.straightLeg.left':amount,'body.straightLeg.right':amount}
  r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'bilateral-field.bin'),render=str(OUT/f'{name}-bilateral-{amount}.png'))
  w,h=r['meshSize'];f=np.fromfile(OUT/'bilateral-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  drift=float(np.linalg.norm((forward_points(f,probes)-probes)*(size-1),axis=1).max());assert drift<.05,(name,amount,drift)
  after=np.asarray(Image.open(OUT/f'{name}-bilateral-{amount}.png'))
  assert np.array_equal(after[region],baseline[region]),(name,amount,'feet pixels changed')
  assert not np.array_equal(after,baseline),(name,amount,'no image effect')
  records.append(dict(case=name,amount=amount,footDrift=drift,footPixelsIdentical=True))
(OUT/'bilateral-metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(records)
