from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-smooth';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['acne-face','jessica-watkins','leroy-chiao','ellen-ochoa']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=list(Image.open(source).size)
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0]
    x,y,w,h=p['face'];bounds=(int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1]))
    cw,ch=bounds[2]-bounds[0],bounds[3]-bounds[1];sheet=Image.new('RGB',(cw*3,ch+30),(30,34,38));draw=ImageDraw.Draw(sheet)
    for col,(version,value) in enumerate([(2,0),(1,100),(2,100)]):
        p['settings']['smoothVersion']=version;p['settings']['parameters']={'skin.smooth':value};path=OUT/f'{name}-v{version}-{value}.png'
        r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path),colorMetrics=True)
        if value==0:assert r['changedPixels']==0
        else:assert r['changedPixels']>100
        crop=Image.open(path).crop(bounds);sheet.paste(crop,(col*cw,30));draw.text((col*cw+4,5),f'smooth v{version} {value}',fill='white')
        records.append(dict(name=name,value=value,version=version,**r))
    sheet.save(OUT/f'{name}-comparison.png')
flat=OUT/'uniform-skin.png';Image.new('RGB',size,(180,115,95)).save(flat)
r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(flat),render=str(OUT/'uniform-result.png'),colorMetrics=True)
assert r['maximumLuminanceError']<2e-6,r
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print('Guided smoothing rendered real samples and preserves a uniform surface.')
