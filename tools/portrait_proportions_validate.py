"""Measure independent facial thirds, stable features and unchanged oral shape."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-proportions';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['leroy-chiao','chris-hadfield','glasses']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=np.array(Image.open(source).size)
    recipe=call(op='portrait_geometry',frameAnalysis=True,source=str(source),recipe=recipe,mask=str(OUT/f'{name}-frames.png'))['recipe']
    p=recipe['people'][0];p['settings']['proportionGeometryVersion']=2
    lm=np.array(p['landmarks'])[:,:2];pixels=lm*size
    axis=pixels[42:48].mean(0)-pixels[36:42].mean(0);axis/=np.linalg.norm(axis);vertical=np.array([-axis[1],axis[0]])
    sheet=Image.new('RGB',(990,1050),(30,34,38));draw=ImageDraw.Draw(sheet)
    for row,key in enumerate(['midface','philtrum','lowerface']):
        for column,value in enumerate([-100,0,100]):
            print('Rendering',name,key,value,flush=True)
            p['settings']['parameters']={'head.'+key:value};path=OUT/f'{name}-{key}-{value}.png'
            r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(path))
            x,y,w,h=p['face'];crop=Image.open(path).crop((int((x-w*.05)*size[0]),int(y*size[1]),int((x+w*1.05)*size[0]),int((y+h*1.15)*size[1])))
            crop.thumbnail((330,320));sheet.paste(crop,(column*330+(330-crop.width)//2,row*350+30));draw.text((column*330+4,row*350+5),f'{key} {value}',fill='white')
            if value==0:assert r['identity'];continue
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            frameMask=np.asarray(Image.open(OUT/f'{name}-frames.png'));fy,fx=np.nonzero(frameMask>127)
            frameDrift=0.
            if len(fx):
                fp=np.column_stack([(fx+.5)/frameMask.shape[1],(fy+.5)/frameMask.shape[0]])
                frameDrift=float(np.linalg.norm(sample(field,fp)*size,axis=1).max())
                assert frameDrift<1.,(name,key,'frame drift',frameDrift)
            target=lm.copy()
            for _ in range(100):target=lm-sample(field,target)
            output=target*size;delta=output-pixels
            fixedDrift=np.linalg.norm(delta[np.r_[17:28,36:48]],axis=1).max()
            assert fixedDrift<.75,(name,key,fixedDrift)
            mouthShift=delta[48:66].mean(0)
            rigidError=np.linalg.norm(delta[48:66]-mouthShift,axis=1).max()
            assert rigidError<.4,(name,key,rigidError)
            if key=='midface':
                assert delta[33]@vertical*value>0
                assert np.linalg.norm(delta[33]-delta[8])<.4
                assert np.linalg.norm(mouthShift-delta[33])<.4
                assert np.linalg.norm(delta[30:36]-delta[33],axis=1).max()<.4
            elif key=='philtrum':
                assert np.linalg.norm(delta[np.r_[0:17,27:36]],axis=1).max()<.5
                assert mouthShift@vertical*value>0
            else:
                assert np.linalg.norm(delta[27:36],axis=1).max()<.5
                assert delta[8]@vertical*value>0
                assert 0<np.linalg.norm(mouthShift)<np.linalg.norm(delta[8])
            records.append(dict(name=name,key=key,value=value,fixedDrift=float(fixedDrift),mouthRigidError=float(rigidError),frameDrift=frameDrift,ms=r['milliseconds']))
    sheet.save(OUT/f'{name}-comparison.jpg')
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
print('Facial thirds passed; inspect comparison images.')
