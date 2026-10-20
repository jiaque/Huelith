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
r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'real-field.bin'),render=str(OUT/'yoga-height.png'))
w,h=r['meshSize'];f=np.fromfile(OUT/'real-field.bin',np.float32).reshape(h,w,4)[:,:,:2];pose=np.array(p['settings']['pose']);pts=pose[[11,12,13,14,15,16,23,24,25,26,27,28,29,30,31,32],:2];shift=(forward_points(f,pts)-pts)*(size-1);gain=((pts[0]+pts[1])*.5-(pts[10]+pts[11])*.5)*(size-1)*.05;expected=np.array([gain*k for k in [1,1,1,1,1,1,.75,.75,.375,.375,0,0,0,0,0,0]]);error=float(np.linalg.norm(shift-expected,axis=1).max());assert error<.5,error
landmarks=np.array(p['landmarks'])[:,:2];headError=float(np.linalg.norm((forward_points(f,landmarks)-landmarks)*(size-1)-gain,axis=1).max());assert headError<.5
print('joints',error,'head',headError,'gain',gain.tolist(),'ms',r['milliseconds'],flush=True);(OUT/'real-metrics.json').write_text(json.dumps({'jointError':error,'headError':headError,'gain':gain.tolist(),'milliseconds':r['milliseconds']},indent=2))

def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
p['settings']['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=3,select=0);before=wait()['recipe'];call(op='portrait_state',parameter='body.height',value=80);after=wait();assert after['recipe']['people'][0]['settings']['parameters']['body.height']==80,after['status'];assert after['recipe']['people'][0]['settings']['heightVersion']==2
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'window.png'),save=str(OUT/'height.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after['recipe']);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'window.png')),np.array(Image.open(OUT/'reopened.png')));call(op='portrait_close')
portable=call(op='portrait_geometry',portableSkin=after['recipe']['people'][0]['settings'],bodyPreset=True,targetSettings={});assert portable['portable']['heightVersion']==2 and portable['merged']['heightVersion']==2
p['settings']['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=3,select=0);before=wait();started=time.monotonic();r=call(op='portrait_state',dragParameter='body.height',dragValues=list(range(0,81,2)),stepMs=30);elapsed=time.monotonic()-started
frames=r['dragFrames'];assert len({str(f['viewport']) for f in frames})==1;assert len({f['zoom'] for f in frames})==1;updates=len({f['frames'] for f in frames})-1;assert updates>=5,(updates,elapsed)
after=wait();assert after['history']==before['history']+1 and after['displayedRevision']==after['previewRevision'];call(op='portrait_state',history=-1);assert wait()['recipe']==before['recipe'];call(op='portrait_close')
(OUT/'drag-metrics.json').write_text(json.dumps({'seconds':elapsed,'intermediateUpdates':updates,'layoutStable':True,'oneUndo':True,'finalRevisionMatches':True},indent=2));print('drag',elapsed,updates,flush=True)
call(op='shutdown');print('Real-photo height UI, history, ROI, save/reopen and preset version passed')
