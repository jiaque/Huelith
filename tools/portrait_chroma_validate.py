from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-skin-chroma';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['leroy-chiao','jessica-watkins','ellen-ochoa','acne-face']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=list(Image.open(source).size)
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0];p['settings']['colorChromaVersion']=2
    sheet=Image.new('RGB',(990,760),(30,34,38));draw=ImageDraw.Draw(sheet)
    for row,key in enumerate(['color.warmth','color.rosy']):
        for col,value in enumerate([-100,0,100]):
            p['settings']['parameters']={key:value};path=OUT/f'{name}-{key}-{value}.png'
            r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path),colorMetrics=True)
            assert r['maximumLuminanceError']<2e-6,(name,key,value,r)
            if value:assert r['changedPixels']>100,(name,key,value,r)
            else:assert r['changedPixels']==0
            x,y,w,h=p['face'];crop=Image.open(path).crop((int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1])))
            crop.thumbnail((330,350));sheet.paste(crop,(col*330+(330-crop.width)//2,row*380+30));draw.text((col*330+4,row*380+5),f'{key} {value}',fill='white')
            records.append(dict(name=name,key=key,value=value,luminanceError=r['maximumLuminanceError'],changed=r['changedPixels']))
    sheet.save(OUT/f'{name}-comparison.jpg')
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print('Chroma changes preserve production FP32 luminance.')
