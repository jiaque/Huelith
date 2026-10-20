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

OUT=ROOT/'docs/test-evidence/M3/semantic-back';source=OUT/'pexels-8059384.jpg';call(op='init',settings=str(OUT/'standing-settings'));call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'))
end=time.monotonic()+120
while time.monotonic()<end:
 call(op='pump',waitMs=30);r=call(op='portrait_state')
 if r['ready'] and not r['previewBusy']:break
else:raise AssertionError('timeout')
(OUT/'full-standing-recipe.json').write_text(json.dumps(r['recipe']),encoding='utf-8')
print('count',r['count'],'status',r['status'],flush=True)
for p in r['recipe']['people']:
 print('partial',p['settings'].get('partialSkin'), 'ambiguous',p.get('bodyAmbiguous'),'pose',len(p['settings'].get('pose',[])),flush=True)
call(op='portrait_state',panel=3,select=0);call(op='portrait_state',parameter='body.back',value=100)
for i in range(200):
 call(op='pump',waitMs=20);r=call(op='portrait_state')
 if not r['previewBusy']:break
print('result',r['status'],(r['recipe']['people'][0]['settings'].get('parameters') or {}).get('body.back'),flush=True)
assert (r['recipe']['people'][0]['settings'].get('parameters') or {}).get('body.back')==100,r['status']
recipe=r['recipe'];size=np.array(Image.open(source).size)
result=call(op='portrait_state',source=str(source),render=str(OUT/'full-standing-after.png'),save=str(OUT/'full-standing.huelith'),checkRegion=True)
assert result['roundtrip'] and result['regionError']<2e-6
call(op='portrait_state',history=-1);call(op='pump',waitMs=200)
assert not (call(op='portrait_state')['recipe']['people'][0]['settings'].get('parameters') or {}).get('body.back')
call(op='portrait_state',history=1);call(op='pump',waitMs=200);assert call(op='portrait_state')['recipe']==recipe
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=recipe)
for i in range(400):
 call(op='pump',waitMs=20);state=call(op='portrait_state')
 if state['ready'] and not state['previewBusy']:break
call(op='portrait_state',source=str(source),render=str(OUT/'full-standing-reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'full-standing-after.png')),np.array(Image.open(OUT/'full-standing-reopened.png')));call(op='portrait_close')
result=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),field=str(OUT/'full-standing-field.bin'))
w,h=result['meshSize'];f=np.fromfile(OUT/'full-standing-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
person=recipe['people'][0];pose=np.array(person['settings']['pose']);landmarks=np.array(person['landmarks'])[:,:2]
fixed=np.concatenate([landmarks,pose[[11,12,13,14,15,16,23,24,25,26,27,28],:2]])
fixedError=float(np.linalg.norm((forward_points(f,fixed)-fixed)*(size-1),axis=1).max());assert fixedError<.15,fixedError
# Contour points selected from the original visible coat edge, independent of
# the extracted mask. Retouch appearance only: no medical posture inference.
back=np.array([[449,750],[461,800],[474,840]],float)/(size-1);shift=(forward_points(f,back)-back)*(size-1)
assert shift[:,0].max()>1,shift
assert np.max(np.abs(shift[:,1]))<1
jacx=np.gradient(f,1/(w-1),axis=1);jacy=np.gradient(f,1/(h-1),axis=0);minimum=float(((1+jacx[:,:,0])*(1+jacy[:,:,1])-jacx[:,:,1]*jacy[:,:,0]).min());assert minimum>.5,minimum
import copy
# The same uncertain limb becomes unsafe when its candidate location moves
# into the back strip; missing joints cannot silently bypass protection.
for missing in [False,True]:
 test=copy.deepcopy(recipe);q=test['people'][0]['settings']['pose'][13];q[:2]=[.38,.43];q[2]=0 if missing else .12
 try:call(op='portrait_geometry',recipe=test,size=size.tolist());raise AssertionError('uncertain posterior arm accepted')
 except RuntimeError as e:assert '手臂' in str(e)
(OUT/'full-standing-result.json').write_text(json.dumps({'automaticEffectPassed':True,'singleSampleOnly':True,'source':'pexels-8059384.jpg','fixedHeadJointsMaxPixels':fixedError,'backContourShiftPixels':shift.tolist(),'minimumJacobian':minimum,'undoRedo':True,'saveReopen':True,'regionConsistent':True,'uncertainCrossingRejected':True},indent=2),encoding='utf-8')
print('fixed',fixedError,'back',shift.tolist(),'minimumJacobian',minimum,flush=True)
# Strength progression must remain monotone without moving neck/coat collar.
progression=[]
collar=np.array([[x,y] for y in range(580,681,20) for x in range(480,641,20)],float)/(size-1)
for amount in [25,50,75,100]:
 test=copy.deepcopy(recipe);test['people'][0]['settings']['parameters']['body.back']=amount
 result=call(op='portrait_geometry',recipe=test,size=size.tolist(),field=str(OUT/'strength-field.bin'))
 mw,mh=result['meshSize'];field=np.fromfile(OUT/'strength-field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
 displacement=(forward_points(field,back)-back)*(size-1)
 collarError=float(np.linalg.norm((forward_points(field,collar)-collar)*(size-1),axis=1).max());assert collarError<.15,collarError
 progression.append({'amount':amount,'contourX':displacement[:,0].tolist(),'collarMax':collarError})
assert np.all(np.diff([v['contourX'] for v in progression],axis=0)>0)
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);state=call(op='portrait_state')
  if state['ready'] and not state['previewBusy']:return state
 raise AssertionError('preview timeout')
base=copy.deepcopy(recipe);base['people'][0]['settings']['parameters']['body.back']=0
call(op='portrait_window',path=str(source),recipe=base);wait();call(op='portrait_state',panel=3,select=0);before=wait()
started=time.monotonic();drag=call(op='portrait_state',dragParameter='body.back',dragValues=list(range(0,101,5)),stepMs=50);elapsed=time.monotonic()-started
frames=drag['dragFrames'];updates=len({x['frames'] for x in frames})-1
assert len({str(x['viewport']) for x in frames})==1 and len({x['zoom'] for x in frames})==1
state=wait();assert state['displayedRevision']==state['previewRevision'];assert state['history']==before['history']+1
assert updates>=3,(updates,elapsed)
call(op='portrait_state',history=-1);assert wait()['recipe']==before['recipe'];call(op='portrait_close')
(OUT/'full-standing-interaction.json').write_text(json.dumps({'strengthProgression':progression,'dragSeconds':elapsed,'intermediateUpdates':updates,'layoutStable':True,'singleUndo':True},indent=2),encoding='utf-8')
print('progressive strength, collar fixed, drag',elapsed,updates,flush=True)
call(op='portrait_window',path=str(source),recipe=base);wait();call(op='portrait_state',panel=3,select=0);wait()
call(op='portrait_state',click='portraitFreeze1',brushSize=120)
call(op='portrait_state',paintPath=[[.384,.433],[.384,.444],[.384,.455]])
frozenBefore=wait()['recipe'];assert frozenBefore['people'][0]['settings'].get('protection')
call(op='portrait_state',parameter='body.back',value=100);frozen=wait();assert frozen['recipe']['people'][0]['settings']['parameters']['body.back']==100,frozen['status']
result=call(op='portrait_geometry',recipe=frozen['recipe'],size=size.tolist(),field=str(OUT/'frozen-field.bin'))
mw,mh=result['meshSize'];ff=np.fromfile(OUT/'frozen-field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
protectedPoint=np.array([[.384,.444]])
protectedError=float(np.linalg.norm(sample(ff,protectedPoint)*(size-1),axis=1).max());assert protectedError<.15,protectedError
result=call(op='portrait_state',source=str(source),render=str(OUT/'full-standing-frozen.png'),save=str(OUT/'full-standing-frozen.huelith'),checkRegion=True);assert result['roundtrip'] and result['regionError']<2e-6
call(op='portrait_state',history=-1);assert wait()['recipe']==frozenBefore
call(op='portrait_close');print('painted freeze area stays fixed',protectedError,flush=True)

call(op='shutdown')
