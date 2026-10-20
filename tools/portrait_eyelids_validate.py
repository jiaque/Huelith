"""Measure each eyebrow control against its distinct objective, using production C++."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-eyelids';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
keys=['eyeUpperLift','eyeLowerLid','eyeOuterLift']
for name in ['leroy-chiao','chris-hadfield','glasses','jessica-watkins','christina-koch','ellen-ochoa']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=np.array(Image.open(source).size)
    recipe=call(op='portrait_geometry',frameAnalysis=True,source=str(source),recipe=recipe,mask=str(OUT/f'{name}-frames.png'))['recipe']
    p=recipe['people'][0];p['settings']['eyeGeometryVersion']=2
    lm=np.array(p['landmarks'])[:,:2];pixels=lm*size
    axis=pixels[42:48].mean(0)-pixels[36:42].mean(0);axis/=np.linalg.norm(axis);vertical=np.array([-axis[1],axis[0]])
    sheet=Image.new('RGB',(990,len(keys)*230),(30,34,38));draw=ImageDraw.Draw(sheet)
    for row,key in enumerate(keys):
        for column,value in enumerate([0,50,100]):
            p['settings']['parameters']={'feature.'+key+'.left':value}
            path=OUT/f'{name}-{key}-{value}.png'
            r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(path))
            x,y,w,h=p['face'];crop=Image.open(path).crop((int((x-w*.1)*size[0]),int(y*size[1]),int((x+w*1.1)*size[0]),int((y+h*.67)*size[1])))
            crop.thumbnail((330,200));sheet.paste(crop,(column*330+(330-crop.width)//2,row*230+30));draw.text((column*330+4,row*230+5),f'{key} {value}',fill='white')
            if value==0:assert r['identity'];continue
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            target=forward_points(field,lm)
            output=target*size;delta=output-pixels
            fixed=np.r_[0:42,48:66];drift=np.linalg.norm(delta[fixed],axis=1).max()
            assert drift<.75,(name,'neighbor drift',drift)
            tangent=pixels[45]-pixels[42];span=np.linalg.norm(tangent);tangent/=span
            normal=np.array([-tangent[1],tangent[0]])
            ratio=np.linalg.norm(output[45]-output[42])/span
            assert abs(ratio-1)<.025,(name,'width changed',ratio)
            originalHeight=(pixels[47]-pixels[43])@normal
            height=(output[47]-output[43])@normal
            if key!='eyeOuterLift':assert (height-originalHeight)*value>0,(name,'opening direction',height-originalHeight)
            center=lm[42:48].mean(0)[None,:]
            centerDrift=float(np.linalg.norm((forward_points(field,center)-center)*size))
            assert centerDrift<.5,(name,'center moved',centerDrift)
            fixedEye=[42,45,46,47] if key=='eyeUpperLift' else [42,43,44,45] if key=='eyeLowerLid' else [42]
            assert np.linalg.norm(delta[fixedEye],axis=1).max()<.35,(name,key,'fixed eye edge drift')
            if key=='eyeOuterLift':assert delta[45]@normal<0,(name,'outer lift direction')
            records.append(dict(name=name,value=value,spanRatio=float(ratio),openingError=float(height-originalHeight),centerDrift=centerDrift,fixedDrift=float(drift)))
            print(records[-1],flush=True)
    sheet.save(OUT/f'{name}-comparison.jpg')
(OUT/'metrics.json').write_text(json.dumps(records,indent=2))
call(op='shutdown');print('Eyelid objectives passed; image review still required.')
