"""Verify eye size, spacing, height and tilt are independent affine objectives."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-eyes';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
records=[]
for name in ['leroy-chiao','chris-hadfield','glasses']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';im=Image.open(source);size=np.array(im.size)
    recipe=call(op='portrait_geometry',frameAnalysis=True,source=str(source),recipe=recipe,mask=str(OUT/f'{name}-frame-mask.png'))['recipe']
    p=recipe['people'][0];p['settings']['eyeGeometryVersion']=2;p['settings']['contourVersion']=2
    lm=np.array(p['landmarks'])[:,:2];pixels=lm*size
    sheet=Image.new('RGB',(3*330,4*300),(30,34,38));draw=ImageDraw.Draw(sheet)
    for row,key in enumerate(['feature.eyeSize','feature.eyeSpace','feature.eyeHeight','feature.eyeTilt']):
        for column,value in enumerate([-80,0,80]):
            p['settings']['parameters']={key+'.left':value,key+'.right':value}
            path=OUT/f'{name}-{key}-{value}.png'
            r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(path))
            x,y,w,h=p['face'];crop=Image.open(path).crop((int((x-w*.1)*size[0]),int(y*size[1]),int((x+w*1.1)*size[0]),int((y+h)*size[1])))
            crop.thumbnail((330,270));sheet.paste(crop,(column*330+(330-crop.width)//2,row*300+30));draw.text((column*330+5,row*300+5),f'{key} {value}',fill='white')
            if value==0:assert r['identity'];continue
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            frameMask=np.asarray(Image.open(OUT/f'{name}-frame-mask.png'))
            fy,fx=np.nonzero(frameMask>127)
            frameDrift=0.
            if len(fx):
                fp=np.column_stack([(fx+.5)/frameMask.shape[1],(fy+.5)/frameMask.shape[0]])
                frameDrift=float(np.linalg.norm(sample(field,fp)*size,axis=1).max())
                assert frameDrift<1.0,(name,key,'frame drift',frameDrift)
            target=lm.copy()
            for _ in range(100):target=lm-sample(field,target)
            output=target*size;fixed=np.r_[0:36,48:66]
            drift=np.linalg.norm(output[fixed]-pixels[fixed],axis=1).max()
            left=pixels[42:48].mean(0);right=pixels[36:42].mean(0)
            leftOut=output[42:48].mean(0);rightOut=output[36:42].mean(0)
            eyeWidth=np.linalg.norm(pixels[45]-pixels[42]);outWidth=np.linalg.norm(output[45]-output[42])
            eyeHeight=np.linalg.norm(pixels[43]-pixels[47]);outHeight=np.linalg.norm(output[43]-output[47])
            sizeRatio=outWidth/eyeWidth;shapeError=abs(sizeRatio-outHeight/eyeHeight)
            centerDrift=max(np.linalg.norm(leftOut-left),np.linalg.norm(rightOut-right))
            spacing=np.linalg.norm(leftOut-rightOut)-np.linalg.norm(left-right)
            if key in ['feature.eyeSize','feature.eyeTilt']:assert centerDrift<.5,(key,centerDrift)
            if key!='feature.eyeSize':assert abs(sizeRatio-1)<.015,(key,sizeRatio)
            if key=='feature.eyeSize':assert abs(sizeRatio-(1+.12*value*.01))<.015
            if key=='feature.eyeSpace':assert spacing*value>0
            assert drift<.75,(name,key,drift)
            assert shapeError<.02,(name,key,shapeError)
            records.append(dict(name=name,key=key,value=value,ms=r['milliseconds'],frameDrift=frameDrift,fixedDrift=float(drift),centerDrift=float(centerDrift),sizeRatio=float(sizeRatio),shapeError=float(shapeError),spacing=float(spacing)))
            print(records[-1],flush=True)
    sheet.save(OUT/f'{name}-comparison.jpg')
(OUT/'metrics.json').write_text(json.dumps(records,indent=2))
call(op='shutdown')
print('Eye control invariants passed; image review still required.')
