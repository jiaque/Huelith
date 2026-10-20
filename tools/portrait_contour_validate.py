"""Exercise production inverse fields and inspect real portrait crops offscreen."""
import os, ctypes, json, time
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M3/semantic-contour'
OUT.mkdir(parents=True,exist_ok=True)
os.environ['QT_QPA_PLATFORM']='offscreen'
if Path('C:/Windows/Fonts').is_dir():
    os.environ.setdefault('QT_QPA_FONTDIR','C:/Windows/Fonts')
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
call(op='init',settings=str(OUT/'settings'))
results=[]
for name in ['leroy-chiao','chris-hadfield','mae-jemison','glasses']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png'
    im=Image.open(source);size=im.size
    for person in recipe['people']:
        person['settings']['parameters']={}
        person['settings']['contourVersion']=2
    p=max(recipe['people'],key=lambda p:p['face'][2]*p['face'][3]);lm=np.array(p['landmarks'])[:,:2]
    eyeaxis=(lm[42:48].mean(0)-lm[36:42].mean(0))*size;eyeaxis/=np.linalg.norm(eyeaxis)
    vertical=np.array([-eyeaxis[1],eyeaxis[0]])
    facewidth=np.linalg.norm((lm[16]-lm[0])*size)
    sheet=Image.new('RGB',(3*330,4*380),(30,34,38));draw=ImageDraw.Draw(sheet)
    for row,key in enumerate(['face.width','face.cheek','face.jaw','face.chin']):
        for column,value in enumerate([-80,0,80]):
            p['settings']['parameters']={key:value}
            path=OUT/f'{name}-{key}-{value}.png'
            r=call(op='portrait_geometry',size=size,recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(path))
            crop=Image.open(path)
            x,y,w,h=p['face'];crop=crop.crop((int((x-w*.25)*size[0]),int((y-h*.3)*size[1]),int((x+w*1.25)*size[0]),int((y+h*1.3)*size[1])))
            crop.thumbnail((330,350));sheet.paste(crop,(column*330+(330-crop.width)//2,row*380+30));draw.text((column*330+5,row*380+5),f'{key} {value}',fill='white')
            if value==0:
                assert r['identity'];continue
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            # Recover forward feature positions by solving target+inverse(target)=source.
            target=lm.copy()
            for _ in range(80):target=lm-sample(field,target)
            residual=np.linalg.norm((target+sample(field,target)-lm)*size,axis=1).max()
            movement=(target-lm)*size
            fixed=np.linalg.norm(movement[17:],axis=1).max()
            dx=np.gradient(field,1/(mw-1),axis=1);dy=np.gradient(field,1/(mh-1),axis=0)
            determinant=(1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]
            record=dict(name=name,key=key,value=value,ms=r['milliseconds'],fixedDriftPixels=float(fixed),inverseResidual=float(residual),minJacobian=float(determinant.min()),chinX=float(movement[8]@eyeaxis),chinY=float(movement[8]@vertical),widthChange=float((movement[13]-movement[3])@eyeaxis)/facewidth)
            results.append(record)
            print(record,flush=True)
    sheet.save(OUT/f'{name}-comparison.jpg')
(OUT/'metrics.json').write_text(json.dumps(results,indent=2))
call(op='shutdown')
assert all(r['minJacobian']>.05 for r in results),'folded or nearly collapsed field'
assert all(r['fixedDriftPixels']<1.0 for r in results),'protected features drift'
assert all(r['inverseResidual']<.1 for r in results),'inverse solver did not converge'
assert all(r['widthChange']*r['value']>0 for r in results if r['key']=='face.width'),'width direction'
assert all(r['chinY']*r['value']>0 and abs(r['chinX'])<1 for r in results if r['key']=='face.chin'),'chin semantics'
print('Contour semantics passed; image review still required.')
