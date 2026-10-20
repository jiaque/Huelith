"""D09 calf must not pull the bent thigh."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import io,base64,copy
OUT=ROOT/'docs/test-evidence/M3/semantic-legs'
call(op='init',settings=str(OUT/'calf-settings'));records=[]
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
    pose=[[.9,.9,0] for _ in range(33)]
    for i,q in zip([27,25,23],joints):pose[i]=[*list(q/scale),1]
    p['settings']['pose']=pose
    for value in [-100,100]:
        p['settings']['parameters']={'body.calf.left':value}
        r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'calf-field.bin'))
        w,h=r['meshSize'];f=np.fromfile(OUT/'calf-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
        q=probes/scale;drift=float(np.linalg.norm((forward_points(f,q)-q)*scale,axis=1).max())
        print(name,value,'thigh drift',drift,flush=True)
        assert drift<.2,(name,value,'thigh moved',drift)
        upper=np.array([[181,325],[219,325]])/scale
        delta=(forward_points(f,upper)-upper)*scale
        assert delta[0,0]*value>0 and delta[1,0]*value<0
        fy,fx=np.gradient(f,axis=(0,1));fx*=w-1;fy*=h-1
        determinant=(1+fx[:,:,0])*(1+fy[:,:,1])-fx[:,:,1]*fy[:,:,0]
        assert determinant.min()>.3,(name,value,'fold',determinant.min())
        records.append(dict(name=name,value=value,thighDrift=drift,thighDelta=delta.tolist()))
# A calf is tapered and asymmetric, not a constant-width cylinder.
size=np.array([640,800]);scale=size-1
samples=[(0,16),(.25,35),(.5,29),(.75,19),(1,9)]
mask=Image.new('L',tuple(size));draw=ImageDraw.Draw(mask)
draw.line([(200,100),(200,300)],fill=255,width=40)
outline=[(200-r,300+t*350) for t,r in samples]+[(200+r*.8,300+t*350) for t,r in reversed(samples)]
draw.polygon(outline,fill=255);draw.rectangle((185,645,250,675),fill=255)
b=io.BytesIO();mask.save(b,format='PNG')
recipe=copy.deepcopy(base);p=recipe['people'][0];recipe['people']=[p]
p['bodyMask']=base64.b64encode(b.getvalue()).decode();p['bodyAmbiguous']=False
p['settings']['automaticMasks']={};p['settings']['limbGeometryVersion']=2;p['settings'].pop('backgroundLines',None)
pose=[[.9,.9,0] for _ in range(33)]
for i,q in [(23,(200,100)),(25,(200,300)),(27,(200,650)),(31,(240,660))]:pose[i]=[*list(np.array(q)/scale),1]
p['settings']['pose']=pose
edges=np.array([[200-r,300+t*350] for t,r in samples]+[[200+r*.8,300+t*350] for t,r in samples])/scale
fixed=np.array([(200,300),(200,650),(200,660),(220,660),(240,660)])/scale
for value in [-100,50,100]:
    p['settings']['parameters']={'body.calf.left':value}
    r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'calf-field.bin'))
    w,h=r['meshSize'];f=np.fromfile(OUT/'calf-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
    moved=forward_points(f,edges)*scale
    widths=moved[5:,0]-moved[:5,0]
    assert widths[1]>widths[2]>widths[3]>widths[4],widths
    original=np.array([r*1.8 for t,r in samples])
    assert all((widths[i]-original[i])*value<0 for i in [1,2,3]),widths
    drift=float(np.linalg.norm((forward_points(f,fixed)-fixed)*scale,axis=1).max())
    assert drift<.2,drift
    records.append(dict(name='tapered',value=value,widths=widths.tolist(),fixedDrift=drift))
# Real walking people: sample thigh interiors across the segment, and render.
source=ROOT/'docs/test-evidence/M3/quality/FudanPed00001-source.png'
base=json.loads((source.parent/'FudanPed00001-recipe.json').read_text())
size=np.array(Image.open(source).size);scale=size-1
for person in range(len(base['people'])):
    for side in range(2):
        recipe=copy.deepcopy(base)
        for p in recipe['people']:p['settings']['parameters']={}
        p=recipe['people'][person];p['settings']['limbGeometryVersion']=2
        pose=np.array(p['settings']['pose']);joints=pose[np.array([23,25,27])+side,:2]*scale
        axis=joints[1]-joints[0];axis/=np.linalg.norm(axis);normal=np.array([-axis[1],axis[0]])
        probes=np.array([joints[0]*(1-t)+joints[1]*t+normal*d for t in np.linspace(0,1,40) for d in [-6,0,6]])/scale
        for value in [-100,50,100]:
            p['settings']['parameters']={'body.calf.'+('left' if side==0 else 'right'):value}
            r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'calf-field.bin'),source=str(source),render=str(OUT/f'calf-{person}-{side}-{value}.png'))
            w,h=r['meshSize'];f=np.fromfile(OUT/'calf-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
            drift=float(np.linalg.norm((forward_points(f,probes)-probes)*scale,axis=1).max())
            assert drift<.2,(person,side,value,'thigh moved',drift)
            assert not r['identity']
            records.append(dict(name='walking',person=person,side=side,value=value,thighDrift=drift))
(OUT/'calf-results.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
call(op='shutdown')
