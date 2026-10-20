from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-redness';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['acne-face','jessica-watkins','leroy-chiao','ellen-ochoa']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=list(Image.open(source).size)
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0];p['settings']['rednessVersion']=2
    sheet=Image.new('RGB',(990,380),(30,34,38));draw=ImageDraw.Draw(sheet)
    for col,value in enumerate([0,50,100]):
        p['settings']['parameters']={'color.redness':value};path=OUT/f'{name}-{value}.png'
        r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path),colorMetrics=True)
        assert r['maximumLuminanceError']<2e-6,(name,value,r)
        if value:assert r['changedPixels']>100,(name,value,r)
        else:assert r['changedPixels']==0
        x,y,w,h=p['face'];crop=Image.open(path).crop((int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1])))
        crop.thumbnail((330,350));sheet.paste(crop,(col*330+(330-crop.width)//2,30));draw.text((col*330+4,5),f'redness {value}',fill='white')
        records.append(dict(name=name,value=value,**r))
    sheet.save(OUT/f'{name}-comparison.jpg')
# A warm but spatially uniform skin sample must not be globally desaturated.
flat=OUT/'uniform-skin.png';Image.new('RGB',size,(180,115,95)).save(flat)
r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(flat),render=str(OUT/'uniform-result.png'),colorMetrics=True)
assert r['changedPixels']==0,r
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print('Local redness correction preserves luminance and uniform skin.')
