"""D05: real contour direction, bone length and hand preservation."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import base64,io
OUT=ROOT/'docs/test-evidence/M3/semantic-arms'
BASE=ROOT/'docs/test-evidence/M3/semantic-limbs'
call(op='init',settings=str(OUT/'quality-settings'))
recipe=json.loads((BASE/'mediapipe-pose-recipe.json').read_text(encoding='utf-8'))
source=BASE/'mediapipe-pose.jpg';size=np.array(Image.open(source).size);scale=size-1
p=recipe['people'][0];p['settings']['limbGeometryVersion']=2
pose=np.array(p['settings']['pose']);mask=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['bodyMask']))).convert('L'))
records=[]
for side in range(2):
    joints=pose[np.array([11,13,15])+side,:2]
    hand=pose[np.array([15,17,19,21])+side,:2]
    # Sample actual mask crossings independently of the width implementation.
    contours=[]
    for a,b in zip(joints[:-1]*scale,joints[1:]*scale):
        axis=(b-a)/np.linalg.norm(b-a);normal=np.array([-axis[1],axis[0]])
        center=(a+b)*.5
        for sign in [-1,1]:
            for d in np.arange(.5,np.linalg.norm(b-a)*.45,.5):
                q=center+normal*sign*d
                xy=np.clip((q/scale*np.array(mask.shape[::-1])).astype(int),0,np.array(mask.shape[::-1])-1)
                if mask[xy[1],xy[0]]<=127:
                    contours.append((q/scale,normal*sign,axis));break
    for value in [25,50,100,-100]:
        p['settings']['parameters']={'body.arm.'+('left' if side==0 else 'right'):value}
        try:
            r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,source=str(source),field=str(OUT/'quality-field.bin'),render=str(OUT/f'real-arm-{side}-{value}.png'))
        except RuntimeError as e:
            records.append(dict(side=side,value=value,refused=str(e)));continue
        w,h=r['meshSize'];field=np.fromfile(OUT/'quality-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
        joint_drift=float(np.linalg.norm((forward_points(field,joints)-joints)*scale,axis=1).max())
        # Include the palm interior, not only detector landmarks.
        palm=np.concatenate([hand,np.array([a*(1-t)+b*t for a in hand for b in hand for t in [.25,.5,.75]])])
        hand_drift=float(np.linalg.norm((forward_points(field,palm)-palm)*scale,axis=1).max())
        assert joint_drift<.8,(side,value,'joint drift',joint_drift)
        assert hand_drift<.8,(side,value,'palm drift',hand_drift)
        shifts=[]
        for q,normal,axis in contours:
            delta=(forward_points(field,np.array([q]))[0]-q)*scale
            radial=float(delta@normal);axial=float(delta@axis)
            assert radial*value<0,(side,value,'contour direction',radial)
            assert abs(axial)<.8,(side,value,'bone direction',axial)
            shifts.append(radial)
        assert len(shifts)==4,(side,'missing contour')
        records.append(dict(side=side,value=value,jointDrift=joint_drift,palmDrift=hand_drift,contourShifts=shifts))
        print(records[-1],flush=True)
assert sum('contourShifts' in r for r in records)>=4,records
(OUT/'real-arm-quality.json').write_text(json.dumps(records,indent=2,ensure_ascii=False),encoding='utf-8')
call(op='shutdown')
