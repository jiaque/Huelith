from pathlib import Path
import io,base64
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-blemish-categories';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());person=recipe['people'][0];person['face']=[.1,.1,.8,.8];person['landmarks']=[]
buf=io.BytesIO();Image.new('L',(128,128),255).save(buf,format='PNG');person['settings']={'partialSkin':True,'automaticMasks':{'skin':base64.b64encode(buf.getvalue()).decode()}}
im=Image.new('RGB',(400,400),(185,145,120));d=ImageDraw.Draw(im)
marks=[(80,90,(177,115,100),'red-texture'),(200,90,(200,135,120),'red-mark'),(320,90,(158,118,85),'pigment'),(80,160,(177,115,100),'red-mark')]
for x,y,c,_ in marks:d.ellipse((x-8,y-8,x+8,y+8),fill=c)
# Same base color and darkness as the fourth mark; only this one has fine texture.
for y in range(84,97):
    for x in range(74,87):
        if (x-80)**2+(y-90)**2<=36:
            delta=6 if (x+y)%2 else -6
            im.putpixel((x,y),tuple(v+delta for v in marks[0][2]))
d.ellipse((75,245,85,255),fill=(65,45,35)) # dark mole, preserved by conservative exclusion
for x in range(160,240,8):d.ellipse((x,250,x+1,251),fill=(175,135,110)) # pores
d.line((280,245,350,245),fill=(150,110,80),width=2) # hair/crease
source=OUT/'controlled.png';im.save(source)
r=call(op='portrait_geometry',source=str(source),person=person,repairAnalysis=True);suggestions=r['blemishes']
(OUT/'controlled-results.json').write_text(json.dumps(suggestions,indent=2))
for x,y,c,category in marks:
    near=[q for q in suggestions if np.hypot(q['x']*400-x,q['y']*400-y)<12]
    assert len(near)==1 and near[0]['category']==category,(category,near)
assert all(q['y']<.5 and q['state']=='pending' and 0<=q['confidence']<=1 for q in suggestions),suggestions
# A user-preserved mark must never return in suggestions.
buf=io.BytesIO();mask=Image.new('L',(400,400),0);ImageDraw.Draw(mask).ellipse((60,70,100,110),fill=255);mask.save(buf,format='PNG');person['settings']['masks']={'repair.preserve':base64.b64encode(buf.getvalue()).decode()}
r=call(op='portrait_geometry',source=str(source),person=person,repairAnalysis=True)
assert not any(np.hypot(q['x']*400-80,q['y']*400-90)<12 for q in r['blemishes'])
for name in ['acne-face','jessica-watkins','john-glenn']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';p=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())['people'][0]
    r=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True);(OUT/f'{name}.json').write_text(json.dumps(r['blemishes'],indent=2))
    im=Image.open(source).convert('RGB');d=ImageDraw.Draw(im)
    for q in r['blemishes']:
        x,y=q['x']*im.width,q['y']*im.height;radius=q['radius']*min(im.size);d.ellipse((x-radius,y-radius,x+radius,y+radius),outline={'red-texture':'red','red-mark':'orange','pigment':'cyan'}[q['category']],width=1)
    im.thumbnail((1000,1000));im.save(OUT/f'{name}-candidates.png')
call(op='shutdown');print('Three visual categories and normal-detail exclusions passed; real candidate overlays saved')
