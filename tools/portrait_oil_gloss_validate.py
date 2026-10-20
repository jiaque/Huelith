from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-c01/algorithm';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['acne-face','jessica-watkins','leroy-chiao','ellen-ochoa']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=list(Image.open(source).size)
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0];p['settings']['oilVersion']=2
    sheet=Image.new('RGB',(990,380),(30,34,38));draw=ImageDraw.Draw(sheet)
    for col,value in enumerate([0,50,100]):
        p['settings']['parameters']={'skin.oil':value};path=OUT/f'{name}-{value}.png'
        r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path),colorMetrics=True)
        if value==0:assert r['changedPixels']==0
        x,y,w,h=p['face'];crop=Image.open(path).crop((int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1])))
        crop.thumbnail((330,350));sheet.paste(crop,(col*330+(330-crop.width)//2,30));draw.text((col*330+4,5),f'oil {value}',fill='white')
        records.append(dict(name=name,value=value,**r))
    sheet.save(OUT/f'{name}-comparison.jpg')
flat=OUT/'uniform-skin.png';Image.new('RGB',size,(180,115,95)).save(flat)
r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(flat),render=str(OUT/'uniform-result.png'),colorMetrics=True)
assert r['changedPixels']==0,r
mask=OUT/'mask.png';call(op='portrait_geometry',recipe=recipe,size=size,styleMask='skin.oil',mask=str(mask))
m=np.asarray(Image.open(mask));ys,xs=np.where(m>230);assert len(xs)>100
cx,cy=xs[len(xs)//2],ys[len(ys)//2];yy,xx=np.mgrid[:size[1],:size[0]]
radius=p['face'][2]*size[0]*.02;spot=np.exp(-((xx-cx)**2+(yy-cy)**2)/(2*radius**2))
linear=np.array([.45,.22,.14])[None,None,:]+spot[:,:,None]*.4
srgb=np.where(linear<=.0031308,linear*12.92,1.055*linear**(1/2.4)-.055)
source=OUT/'specular.png';Image.fromarray(np.uint8(np.clip(srgb*255,0,255))).save(source)
r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(OUT/'specular-result.png'),colorMetrics=True)
assert r['changedPixels']>30 and r['maximumLuminanceError']>.01,r
records.append(dict(name='synthetic-neutral-highlight',**r))
full=np.asarray(Image.open(OUT/'specular-result.png')).astype(float)
p['settings']['parameters']['skin.oilGloss']=100
call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(OUT/'retained.png'))
retained=np.asarray(Image.open(OUT/'retained.png')).astype(float);original=np.asarray(Image.open(source).convert('RGBA')).astype(float)
assert np.abs(original-retained).sum()<np.abs(original-full).sum()
assert np.abs(original-retained).sum()>0
assert np.max(abs(retained-full))>0
# Painting beyond skin must not expand the effect onto clothing/background.
import base64,io
buf=io.BytesIO();Image.new('L',size,255).save(buf,format='PNG');p['settings']['masks']={'skin.oil':base64.b64encode(buf.getvalue()).decode()}
call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(OUT/'paint-all.png'))
assert np.array_equal(np.asarray(Image.open(OUT/'paint-all.png')),np.asarray(Image.open(OUT/'retained.png')))

(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print('Oil correction targets local highlights and leaves uniform skin unchanged.')

