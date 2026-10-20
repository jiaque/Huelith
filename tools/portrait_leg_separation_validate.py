"""D07: adjacent leg, joints and shoe must survive single-leg edits."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import io,base64,copy
OUT=ROOT/'docs/test-evidence/M3/semantic-legs';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
size=np.array([640,800]);scale=size-1
base=json.loads((ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json').read_text())
records=[]
for gap in [8,20,60]:
    x=240;other=x+40+gap
    mask=Image.new('L',tuple(size));d=ImageDraw.Draw(mask)
    for cx in [x,other]:
        d.rectangle((cx-20,150,cx+20,650),fill=255)
        d.rectangle((cx-20,650,cx+30,690),fill=255)
    im=Image.new('RGBA',tuple(size),(28,32,38,255));im.paste((180,135,100,255),mask=mask);source=OUT/f'gap-{gap}-source.png';im.save(source)
    b=io.BytesIO();mask.save(b,format='PNG');recipe=copy.deepcopy(base);p=recipe['people'][0];recipe['people']=[p]
    p['bodyMask']=base64.b64encode(b.getvalue()).decode();p['bodyAmbiguous']=False
    p['settings']['automaticMasks']={};p['settings']['limbGeometryVersion']=2;p['settings'].pop('backgroundLines',None)
    pose=[[.9,.9,1] for _ in range(33)]
    for side,cx in enumerate([x,other]):
        for index,y in [(23,150),(25,400),(27,650),(29,680),(31,680)]:pose[index+side]=[cx/639,y/799,1]
    p['settings']['pose']=pose
    for value,linked in [(v,linked) for linked in [False,True] for v in [-100,100]]:
        p['settings']['parameters']={'body.legWidth.left':value,**({'body.legWidth.right':value} if linked else {})}
        r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'gap-{gap}-{value}.png'))
        w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
        fixed=np.array(([] if linked else [(other+dx,y) for dx in [-19,0,19] for y in np.linspace(150,680,80)])+[(cx+dx,y) for cx in [x,other] for dx in [-19,0,29] for y in [655,670,685]]+[(cx,y) for cx in [x,other] for y in [150,400,650]])/scale
        drift=float(np.linalg.norm((forward_points(f,fixed)-fixed)*scale,axis=1).max())
        print(gap,value,'protected drift',drift,flush=True)
        assert drift<.2,(gap,value,drift)
        edge=np.array([[x-19,275],[x+19,275],[other-19,275],[other+19,275]])/scale
        moved=forward_points(f,edge)*scale;delta=moved-edge*scale
        assert delta[0,0]*value>0 and delta[1,0]*value<0,(gap,value,'edited contour did not move')
        assert moved[2,0]-moved[1,0]>0,(gap,value,'legs merged')
        fy,fx=np.gradient(f,axis=(0,1));fx*=w-1;fy*=h-1
        determinant=(1+fx[:,:,0])*(1+fy[:,:,1])-fx[:,:,1]*fy[:,:,0]
        assert determinant.min()>.3,(gap,value,'fold')
        records.append(dict(gap=gap,value=value,linked=linked,protectedDrift=drift,remainingGap=float(moved[2,0]-moved[1,0]),jacobianMinimum=float(determinant.min())))
(OUT/'separation-results.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
call(op='shutdown')
