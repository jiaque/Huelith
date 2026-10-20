"""D05 bent-arm composition and palm invariants, production field."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import io,base64,copy
OUT=ROOT/'docs/test-evidence/M3/semantic-arms'
call(op='init',settings=str(OUT/'bent-settings'));records=[]
size=np.array([640,800]);scale=size-1
base=json.loads((ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json').read_text())
for name,wrist in [('right-angle',(450,450)),('acute',(400,250)),('obtuse',(400,600))]:
    joints=np.array([(200,200),(200,450),wrist],dtype=float)
    axis=(joints[2]-joints[1]);axis/=np.linalg.norm(axis);normal=np.array([-axis[1],axis[0]])
    palm=np.array([joints[2]+axis*t+normal*d for t in [8,25,45] for d in [-12,0,12]])
    mask=Image.new('L',tuple(size));draw=ImageDraw.Draw(mask)
    draw.line([tuple(q) for q in joints],fill=255,width=40)
    hand=[joints[2]+axis*t+normal*d for t,d in [(0,-18),(55,-18),(55,18),(0,18)]]
    draw.polygon([tuple(q) for q in hand],fill=255)
    im=Image.new('RGBA',tuple(size),(28,32,38,255));im.paste((180,135,100,255),mask=mask)
    source=OUT/(name+'-source.png');im.save(source)
    b=io.BytesIO();mask.save(b,format='PNG')
    recipe=copy.deepcopy(base);p=recipe['people'][0];recipe['people']=[p]
    p['bodyMask']=base64.b64encode(b.getvalue()).decode();p['bodyAmbiguous']=False
    p['settings']['automaticMasks']={};p['settings']['limbGeometryVersion']=2
    p['settings'].pop('backgroundLines',None)
    pose=[[.9,.9,1] for _ in range(33)]
    for i,q in zip([11,13,15],joints):pose[i]=[*list(q/scale),1]
    p['settings']['pose']=pose
    for value in [-100,50,100]:
        p['settings']['parameters']={'body.arm.left':value}
        r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'bent-field.bin'),source=str(source),render=str(OUT/f'{name}-{value}.png'))
        w,h=r['meshSize'];f=np.fromfile(OUT/'bent-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
        probes=np.concatenate([joints,palm])/scale;delta=(forward_points(f,probes)-probes)*scale
        assert np.linalg.norm(delta,axis=1).max()<.2,(name,value,'joint/palm moved',delta.tolist())
        shifts=[]
        for a,b in zip(joints[:-1],joints[1:]):
            direction=(b-a)/np.linalg.norm(b-a);n=np.array([-direction[1],direction[0]])
            for side in [-1,1]:
                q=((a+b)/2+n*side*19)/scale
                movement=(forward_points(f,np.array([q]))[0]-q)*scale
                radial=float(movement@(n*side));axial=float(movement@direction)
                assert radial*value<0 and abs(axial)<.2,(name,value,radial,axial)
                shifts.append(radial)
        fy,fx=np.gradient(f,axis=(0,1));fx*=w-1;fy*=h-1
        determinant=(1+fx[:,:,0])*(1+fy[:,:,1])-fx[:,:,1]*fy[:,:,0]
        assert determinant.min()>.3,(name,value,'fold',determinant.min())
        records.append(dict(name=name,value=value,fixedDrift=float(np.linalg.norm(delta,axis=1).max()),shifts=shifts,jacobianMinimum=float(determinant.min())))
(OUT/'bent-results.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
call(op='shutdown');print('Bent arm widths, joints, palm and non-folding passed.')
