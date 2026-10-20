"""Hairline-based forehead, crown and hairline-position objectives are distinct."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-scalp';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['leroy-chiao','chris-hadfield','glasses','jessica-watkins','christina-koch','ellen-ochoa']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=np.array(Image.open(source).size)
    recipe=call(op='portrait_geometry',headAnalysis=True,recipe=recipe,mask=str(OUT/f'{name}-head.png'))['recipe']
    p=recipe['people'][0];p['settings']['scalpGeometryVersion']=2
    geometry=p['settings']['scalpGeometry'];assert geometry['valid'],(name,geometry)
    lm=np.array(p['landmarks'])[:,:2];pixels=lm*size
    axis=pixels[42:48].mean(0)-pixels[36:42].mean(0);axis/=np.linalg.norm(axis);vertical=np.array([-axis[1],axis[0]])
    line=np.array(geometry['hairline']);top=np.array([geometry['top']])
    sheet=Image.new('RGB',(990,1400),(30,34,38));draw=ImageDraw.Draw(sheet)
    for row,key in enumerate(['face.forehead','head.crown','head.hairline','head.hairVolume']):
        for column,value in enumerate([-100,0,100]):
            print('Rendering',name,key,value,flush=True)
            p['settings']['parameters']={key:value};path=OUT/f'{name}-{key}-{value}.png'
            r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(path))
            x,y,w,h=p['face'];crop=Image.open(path).crop((int((x-w*.35)*size[0]),max(0,int((y-h*.6)*size[1])),int((x+w*1.35)*size[0]),int((y+h*1.5)*size[1])))
            crop.thumbnail((330,320));sheet.paste(crop,(column*330+(330-crop.width)//2,row*350+30));draw.text((column*330+4,row*350+5),f'{key} {value}',fill='white')
            if value==0:assert r['identity'];continue
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            output=forward_points(field,lm)*size
            fixedDrift=float(np.linalg.norm(output-pixels,axis=1).max());assert fixedDrift<.75,(name,key,'fixed face',fixedDrift)
            lineDelta=(forward_points(field,line)-line)*size;topDelta=(forward_points(field,top)-top)*size
            if key=='head.hairVolume':
                assert np.linalg.norm(lineDelta,axis=1).max()<.75,(name,'hairline moved',lineDelta)
                assert (topDelta[0]@vertical)*value<0,(name,'head volume direction',topDelta)
            elif key=='head.crown':
                assert np.linalg.norm(lineDelta,axis=1).max()<.75,(name,'hairline moved',lineDelta)
                assert (topDelta[0]@vertical)*value<0
            elif key=='head.hairline':
                assert (lineDelta.mean(0)@vertical)*value>0
                assert np.linalg.norm(topDelta)<1.,(name,'top moved',topDelta)
            else:
                assert (lineDelta.mean(0)@vertical)*value<0
                assert (topDelta[0]@vertical)*value<0
            records.append(dict(name=name,key=key,value=value,fixedDrift=fixedDrift,hairlineMotion=float(lineDelta.mean(0)@vertical),topMotion=float(topDelta[0]@vertical),ms=r['milliseconds']))
    sheet.save(OUT/f'{name}-comparison.jpg')
# Missing/cropped data must fail explicitly, never fabricate a hairline from a face box.
for geometry in [{'valid':False,'reason':'test missing hairline'},{**geometry,'cropped':True}]:
    p['settings']['scalpGeometry']=geometry;p['settings']['parameters']={'head.crown':50}
    try:call(op='portrait_geometry',size=size.tolist(),recipe=recipe)
    except RuntimeError:pass
    else:raise AssertionError('invalid scalp geometry was accepted')
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
print('Scalp objectives and invalid-data refusal passed; inspect images.')
