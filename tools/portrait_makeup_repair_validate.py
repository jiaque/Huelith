from pathlib import Path
import cv2, copy
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-makeup-repair';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={};p['settings']['mouthTopologyVersion']=2
w,h=Image.open(source).size
records=[]
for part in ['lip.upper','lip.lower','brow.left','brow.right','shadow.left','shadow.right']:
    maskfile=OUT/(part+'.png');call(op='portrait_geometry',person=p,makeupRepairMask=part,width=w,height=h,output=str(maskfile));mask=np.array(Image.open(maskfile));dist=cv2.distanceTransform(np.uint8(mask>200),cv2.DIST_L2,5);y,x=np.unravel_index(dist.argmax(),dist.shape)
    radius=max(1.,float(dist[y,x])*.3)
    stroke=dict(version=1,points=[[x/w,y/h]],offsetX=0,offsetY=0,radius=radius/min(w,h),feather=.5,opacity=1)
    try:
        matched=call(op='portrait_geometry',source=str(source),person=p,makeupRepair=stroke)['stroke']
        assert matched['makeupPart']==part,(part,matched)
        bad=dict(stroke);bad['offsetY']=-.2
        try:call(op='portrait_geometry',source=str(source),person=p,makeupRepair=bad,manualSource=True);raise AssertionError('cross-part source accepted')
        except RuntimeError:pass
        records.append(dict(part=part,target=[x/w,y/h],radius=radius,stroke=matched))
    except RuntimeError as e:raise AssertionError((part,dist[y,x],str(e)))
(OUT/'cases.json').write_text(json.dumps(records,indent=2))
call(op='shutdown');print('Six semantic parts: automatic source and cross-part rejection passed')
