"""D06 must preserve the neighbouring forearm, including a bent elbow."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import io,base64,copy
OUT=ROOT/'docs/test-evidence/M3/semantic-arms'
call(op='init',settings=str(OUT/'upper-settings'));records=[]
size=np.array([640,800]);scale=size-1
base=json.loads((ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json').read_text())
for name,wrist in [('right-angle',(450,450)),('acute',(400,250)),('obtuse',(400,600))]:
    joints=np.array([(200,200),(200,450),wrist],dtype=float)
    axis=(joints[2]-joints[1]);axis/=np.linalg.norm(axis);normal=np.array([-axis[1],axis[0]])
    probes=np.array([joints[1]*(1-t)+joints[2]*t+normal*d for t in np.linspace(.02,1,30) for d in [-18,0,18]])
    mask=Image.new('L',tuple(size));draw=ImageDraw.Draw(mask);draw.line([tuple(q) for q in joints],fill=255,width=40)
    b=io.BytesIO();mask.save(b,format='PNG')
    recipe=copy.deepcopy(base);p=recipe['people'][0];recipe['people']=[p]
    p['bodyMask']=base64.b64encode(b.getvalue()).decode();p['bodyAmbiguous']=False
    p['settings']['automaticMasks']={};p['settings']['limbGeometryVersion']=2;p['settings'].pop('backgroundLines',None)
    pose=[[.9,.9,1] for _ in range(33)]
    for i,q in zip([11,13,15],joints):pose[i]=[*list(q/scale),1]
    p['settings']['pose']=pose
    for value in [-100,100]:
        p['settings']['parameters']={'body.upperArm.left':value}
        r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'upper-field.bin'))
        w,h=r['meshSize'];f=np.fromfile(OUT/'upper-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
        q=probes/scale;drift=float(np.linalg.norm((forward_points(f,q)-q)*scale,axis=1).max())
        print(name,value,'forearm drift',drift,flush=True)
        assert drift<.2,(name,value,'forearm moved',drift)
        upper=np.array([[181,325],[219,325]])/scale
        delta=(forward_points(f,upper)-upper)*scale
        assert delta[0,0]*value>0 and delta[1,0]*value<0
        fy,fx=np.gradient(f,axis=(0,1));fx*=w-1;fy*=h-1
        determinant=(1+fx[:,:,0])*(1+fy[:,:,1])-fx[:,:,1]*fy[:,:,0]
        assert determinant.min()>.3,(name,value,'fold',determinant.min())
        records.append(dict(name=name,value=value,forearmDrift=drift,upperDelta=delta.tolist()))
source=ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg'
size=np.array(Image.open(source).size);scale=size-1
for side in range(2):
    recipe=copy.deepcopy(base);p=recipe['people'][0];p['settings']['limbGeometryVersion']=2
    pose=np.array(p['settings']['pose']);joints=pose[np.array([11,13,15])+side,:2]*scale
    axis=joints[2]-joints[1];axis/=np.linalg.norm(axis);normal=np.array([-axis[1],axis[0]])
    forearm=np.array([joints[1]*(1-t)+joints[2]*t+normal*d for t in np.linspace(0,1,40) for d in [-12,0,12]])/scale
    palm=pose[np.array([17,19,21])+side,:2]
    for value in [-100,25,50,100]:
        p['settings']['parameters']={'body.upperArm.'+('left' if side==0 else 'right'):value}
        r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'upper-field.bin'),source=str(source),render=str(OUT/f'real-upper-{side}-{value}.png'))
        w,h=r['meshSize'];f=np.fromfile(OUT/'upper-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
        probes=np.concatenate([forearm,palm,joints/scale]);delta=(forward_points(f,probes)-probes)*scale
        drift=float(np.linalg.norm(delta,axis=1).max());assert drift<.2,(side,value,drift)
        assert not r['identity']
        records.append(dict(name='real',side=side,value=value,protectedDrift=drift))
(OUT/'upper-results.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
call(op='shutdown')
