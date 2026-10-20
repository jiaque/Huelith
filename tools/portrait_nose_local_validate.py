"""Nose width translates alar sides; length preserves root and moves base."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-nose-local';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['leroy-chiao','chris-hadfield','glasses','jessica-watkins','christina-koch','ellen-ochoa']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=np.array(Image.open(source).size)
    p=recipe['people'][0];p['settings']['noseGeometryVersion']=2;lm=np.array(p['landmarks'])[:,:2];pixels=lm*size
    sheet=Image.new('RGB',(990,960),(30,34,38));draw=ImageDraw.Draw(sheet)
    for row,key in enumerate(['feature.noseRootWidth','feature.noseBridgeWidth','feature.noseTipWidth']):
        for column,value in enumerate([-100,0,100]):
            p['settings']['parameters']={key:value};path=OUT/f'{name}-{key}-{value}.png'
            r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(path))
            x,y,w,h=p['face'];crop=Image.open(path).crop((int((x-w*.1)*size[0]),int(y*size[1]),int((x+w*1.1)*size[0]),int((y+h)*size[1])))
            crop.thumbnail((330,290));sheet.paste(crop,(column*330+(330-crop.width)//2,row*320+30));draw.text((column*330+5,row*320+5),f'{key} {value}',fill='white')
            if value==0:assert r['identity'];continue
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            target=forward_points(field,lm)
            output=target*size;fixed=np.r_[0:28,36:66];drift=np.linalg.norm(output[fixed]-pixels[fixed],axis=1).max()
            widthRatio=np.linalg.norm(output[35]-output[31])/np.linalg.norm(pixels[35]-pixels[31])
            lengthRatio=np.linalg.norm(output[33]-output[27])/np.linalg.norm(pixels[33]-pixels[27])
            nostrilRatio=np.linalg.norm(output[32]-output[31])/np.linalg.norm(pixels[32]-pixels[31])
            assert drift<.75,(name,key,drift)
            assert abs(nostrilRatio-1)<.025,(name,key,nostrilRatio)
            assert abs(widthRatio-1)<.01 and abs(lengthRatio-1)<.01,(name,key,'alar or length changed')
            index=27 if key.endswith('RootWidth') else 29 if key.endswith('BridgeWidth') else 30
            span=np.linalg.norm(pixels[35]-pixels[31]);axis=pixels[42:48].mean(0)-pixels[36:42].mean(0);axis/=np.linalg.norm(axis)
            radius=span*(.20 if index==30 else .25)
            probes=np.array([(pixels[index]-axis*radius)/size,(pixels[index]+axis*radius)/size])
            moved=forward_points(field,probes)*size
            localRatio=np.linalg.norm(moved[1]-moved[0])/(2*radius)
            assert (localRatio-1)*value>0,(name,key,'local width direction',localRatio)
            records.append(dict(name=name,key=key,value=value,ms=r['milliseconds'],fixedDrift=float(drift),widthRatio=float(widthRatio),lengthRatio=float(lengthRatio),nostrilRatio=float(nostrilRatio)))
            print(records[-1],flush=True)
    sheet.save(OUT/f'{name}-comparison.jpg')
(OUT/'metrics.json').write_text(json.dumps(records,indent=2))
call(op='shutdown');print('Nose semantic invariants passed; image review still required.')
