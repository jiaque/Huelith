"""Validate OpenSeeFace lip topology, independent thickness and protected cavity."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-mouth';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['leroy-chiao','chris-hadfield','glasses']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=np.array(Image.open(source).size)
    p=recipe['people'][0];p['settings'].update(mouthGeometryVersion=2,mouthTopologyVersion=2)
    lm=np.array(p['landmarks'])[:,:2];pixels=lm*size
    sheet=Image.new('RGB',(990,960),(30,34,38));draw=ImageDraw.Draw(sheet)
    for row,key in enumerate(['feature.lipSize','feature.lipThickness','feature.smile']):
        for column,value in enumerate([-80,0,80]):
            p['settings']['parameters']=({key+'.left':value,key+'.right':value} if key.endswith('smile') else {key:value})
            path=OUT/f'{name}-{key}-{value}.png'
            r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(path))
            x,y,w,h=p['face'];crop=Image.open(path).crop((int((x-w*.1)*size[0]),int(y*size[1]),int((x+w*1.1)*size[0]),int((y+h)*size[1])))
            crop.thumbnail((330,290));sheet.paste(crop,(column*330+(330-crop.width)//2,row*320+30));draw.text((column*330+5,row*320+5),f'{key} {value}',fill='white')
            if value==0:assert r['identity'];continue
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            target=lm.copy()
            for _ in range(100):target=lm-sample(field,target)
            output=target*size;fixed=np.r_[0:48,59:62,63:66];drift=np.linalg.norm(output[fixed]-pixels[fixed],axis=1).max()
            widthRatio=np.linalg.norm(output[62]-output[58])/np.linalg.norm(pixels[62]-pixels[58])
            lipRatio=np.linalg.norm(output[50]-output[55])/np.linalg.norm(pixels[50]-pixels[55])
            assert drift<.75,(name,key,drift)
            if key=='feature.lipThickness':assert (lipRatio-1)*value>0 and abs(widthRatio-1)<.01
            if key=='feature.lipSize':assert (widthRatio-1)*value>0 and (lipRatio-1)*value>0
            if key=='feature.smile':assert (output[62,1]-pixels[62,1])*value<0
            records.append(dict(name=name,key=key,value=value,ms=r['milliseconds'],fixedDrift=float(drift),widthRatio=float(widthRatio),lipRatio=float(lipRatio)))
            print(records[-1],flush=True)
    sheet.save(OUT/f'{name}-comparison.jpg')
(OUT/'metrics.json').write_text(json.dumps(records,indent=2))
call(op='shutdown');print('Mouth semantic invariants passed; image review still required.')
