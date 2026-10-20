"""Visible turning, not a common translation; inspect the generated portrait sheets."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],__file__,'exec'))
OUT=ROOT/'build/orientation-v2';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
records=[]
try:
    for name in ['chris-hadfield','leroy-chiao','glasses']:
        source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png'
        size=np.array(Image.open(source).size)
        recipe=json.loads((ROOT/f'docs/test-evidence/M3/workspace-b01/orientation/{name}-recipe.json').read_text(encoding='utf-8'))
        person=recipe['people'][0];person['settings']['headOrientationVersion']=2
        points=np.array(person['settings']['faceSurface']['points'])[:,:2]
        x,y,w,h=person['face'];crop=(int((x-w*.25)*size[0]),int((y-h*.5)*size[1]),int((x+w*1.25)*size[0]),int((y+h*1.3)*size[1]))
        sheet=Image.new('RGB',(1200,660),(30,34,38));draw=ImageDraw.Draw(sheet)
        for row,key in enumerate(['head.yaw','head.pitch']):
            displacement={}
            for column,value in enumerate([-100,-50,0,50,100]):
                person['settings']['parameters']={key:value}
                path=OUT/f'{name}-{key}-{value}.png'
                r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,source=str(source),render=str(path),field=str(OUT/'field.bin'))
                assert r['identity']==(value==0)
                mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2] if mw and mh else np.zeros((2,2,2))
                moved=forward_points(field,points);d=(moved-points)*size
                displacement[value]=d
                mean=d.mean(0);relative=np.sqrt(np.mean(np.sum((d-mean)**2,axis=1)))
                if value:
                    assert relative>np.linalg.norm(mean),(name,key,value,mean,relative)
                    assert np.isfinite(field).all()
                    assert np.max(abs(field[0]))==0 and np.max(abs(field[-1]))==0
                    # Nose must move relative to eye corners, not simply carry
                    # the whole facial surface to a different screen position.
                    differential=d[1]-(d[33]+d[263])*.5
                    axis=0 if key=='head.yaw' else 1
                    assert abs(differential[axis])>.002*w*size[0],(name,key,differential)
                records.append(dict(name=name,key=key,value=value,translation=mean.tolist(),relativeRms=float(relative)))
                thumb=Image.open(path).crop(crop);thumb.thumbnail((240,300));sheet.paste(thumb,(column*240,row*330+25));draw.text((column*240+5,row*330+5),f'{key} {value}',fill='white')
            # Both signs must turn in opposite directions.
            assert np.sum(displacement[-100]*displacement[100])<0
        sheet.save(OUT/f'{name}-comparison.png')
    (OUT/'result.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
    print('Three portraits: relative turning, both signs, neutral identity and fixed image edges passed')
finally:call(op='shutdown')
