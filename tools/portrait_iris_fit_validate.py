from pathlib import Path
import base64,io
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-iris-fit';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];p['face']=[0,0,1,1];p['settings']['parameters']={};p['landmarks']=[[.05,.05,1] for _ in range(66)]
im=Image.new('RGB',(400,200),(185,145,120));d=ImageDraw.Draw(im)
for start,cx in [(36,100),(42,300)]:
    coords=[(cx-40,100),(cx-20,80),(cx+20,80),(cx+40,100),(cx+20,120),(cx-20,120)]
    for i,(x,y) in enumerate(coords):p['landmarks'][start+i]=[x/400,y/200,1]
    d.polygon(coords,fill=(230,225,220))
d.ellipse((100,85,130,115),fill=(50,80,110));d.ellipse((110,95,120,105),fill=(10,10,10));d.ellipse((116,89,120,93),fill=(255,255,255))
source=OUT/'shifted-eye.png';im.save(source)
r=call(op='portrait_geometry',repairAnalysis=True,source=str(source),person=p)['irisMaterials'];(OUT/'geometry.json').write_text(json.dumps({k:v for k,v in r.items() if k.startswith('geometry')},indent=2))
g=r['geometry.right'];assert g['valid'],r.keys();x,y,rx,ry,angle=g['ellipse'];assert abs(x*400-115)<3 and abs(y*200-100)<3,g
assert not r['geometry.left']['valid'],'no iris should not be invented in empty eye'
mask=Image.open(io.BytesIO(base64.b64decode(r['iris.right'])));mask.save(OUT/'mask.png');a=np.asarray(mask.resize((400,200)))
assert a[100,115]<10 and a[91,118]<10 and a[100,90]==0
assert a[100,126]>100
p['settings']['irisMaterials']=r;p['settings']['irisMaterialVersion']=2;p['settings']['parameters']={'makeup.iris.right':100}
call(op='portrait_geometry',source=str(source),recipe=recipe,size=[400,200],render=str(OUT/'tinted.png'))
# Catchlight shapes share the fitted ellipse and must be clipped after rasterization.
shape_masks=[]
visible_image=Image.open(io.BytesIO(base64.b64decode(r['cornea.right'])))
for material in ['circle','window','ring']:
    p['settings']['catchMaterial']=material;p['settings']['parameters']={'makeup.catchlight.right':100,'makeup.catchX.right':70,'makeup.catchSize.right':100}
    path=OUT/f'catch-{material}.png';call(op='portrait_geometry',recipe=recipe,size=list(visible_image.size),styleMask='makeup.catchlight.right',mask=str(path));m=np.asarray(Image.open(path));shape_masks.append(m)
    visible=np.asarray(visible_image)
    assert m.max()>0 and np.max(m[visible==0],initial=0)==0
assert not np.array_equal(shape_masks[0],shape_masks[1]) and not np.array_equal(shape_masks[1],shape_masks[2])
buf=io.BytesIO();Image.new('L',(400,200),255).save(buf,format='PNG');p['settings']['automaticMasks']['eyeglassFrame']=base64.b64encode(buf.getvalue()).decode()
path=OUT/'frame-blocked.png';call(op='portrait_geometry',recipe=recipe,size=list(visible_image.size),styleMask='makeup.catchlight.right',mask=str(path));assert np.asarray(Image.open(path)).max()==0
call(op='shutdown');print('Shifted iris fit, no invented iris, pupil and highlight protection passed')

