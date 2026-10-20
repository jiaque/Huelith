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



import io,base64,copy
# Controlled visible backdrop edge, followed by the production recognizer.
original=ROOT/'docs/test-evidence/M3/semantic-back/mr-yoga-side-bend.jpg'
im=Image.open(original).convert('RGB');draw=ImageDraw.Draw(im)
draw.line([(850,600),(850,1100)],fill=(45,45,45),width=3)
source=OUT/'backdrop-source.png';im.save(source);size=np.array(im.size)
call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'))
end=time.monotonic()+120
while time.monotonic()<end:
 call(op='pump',waitMs=20);state=call(op='portrait_state')
 if state['ready'] and not state['previewBusy']:break
else:raise AssertionError('recognition timeout')
recipe=state['recipe'];call(op='portrait_close')
(OUT/'backdrop-detected-recipe.json').write_text(json.dumps(recipe))
p=recipe['people'][0];p['settings']['parameters']={'body.height':100};p['settings']['heightVersion']=2
mask=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['bodyMask']))).convert('L'))
targets=[]
for line in p['settings']['backgroundLines']:
 a,b=np.array(line[:2]),np.array(line[2:]);points=np.array([a+(b-a)*t for t in np.linspace(0,1,9)])
 xy=np.clip((points*np.array(mask.shape[::-1])).astype(int),0,np.array(mask.shape[::-1])-1)
 if np.max(mask[xy[:,1],xy[:,0]])>=25:continue
 targets.extend([a+(b-a)*t for t in np.linspace(0,1,101)])
assert targets,'no real background lines'
fields=[]
for protect in [False,True]:
 p['settings']['protectBackground']=protect
 r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'auto-background-field.bin'),render=str(OUT/f'auto-background-{protect}.png'))
 w,h=r['meshSize'];fields.append(np.fromfile(OUT/'auto-background-field.bin',np.float32).reshape(h,w,4)[:,:,:2])
q=np.array(targets);raw=np.linalg.norm(sample(fields[0],q)*(size-1),axis=1);active=raw>.5
assert active.any(),'no detected line in moving background'
protected=np.linalg.norm(sample(fields[1],q[active])*(size-1),axis=1)
result=dict(activeSamples=int(active.sum()),unprotectedMax=float(raw.max()),protectedMax=float(protected.max()))
print(result,flush=True);assert result['protectedMax']<.2
(OUT/'auto-background-metrics.json').write_text(json.dumps(result,indent=2));call(op='shutdown')
