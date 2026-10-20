"""Independent mouth objectives and rigid oral cavity through the production warp."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-mouth-extended';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
keys=['lipWidth','lipHeight','lipOffset','lipTilt','lipUpper','lipLower','lipCupid']
for name in ['leroy-chiao','chris-hadfield','glasses']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=np.array(Image.open(source).size)
    p=recipe['people'][0];p['settings'].update(mouthGeometryVersion=2,mouthTopologyVersion=2)
    lm=np.array(p['landmarks'])[:,:2];pixels=lm*size
    axis=pixels[42:48].mean(0)-pixels[36:42].mean(0);axis/=np.linalg.norm(axis);vertical=np.array([-axis[1],axis[0]])
    inner=np.array([59,60,61,63,64,65]);center=(pixels[58]+pixels[62])*.5
    sheet=Image.new('RGB',(990,len(keys)*270),(30,34,38));draw=ImageDraw.Draw(sheet)
    for row,key in enumerate(keys):
        for column,value in enumerate([-100,0,100]):
            print('Rendering',name,key,value,flush=True)
            p['settings']['parameters']={'feature.'+key:value};path=OUT/f'{name}-{key}-{value}.png'
            r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(path))
            x,y,w,h=p['face'];crop=Image.open(path).crop((int((x-w*.05)*size[0]),int((y+h*.40)*size[1]),int((x+w*1.05)*size[0]),int((y+h)*size[1])))
            crop.thumbnail((330,240));sheet.paste(crop,(column*330+(330-crop.width)//2,row*270+30));draw.text((column*330+4,row*270+5),f'{key} {value}',fill='white')
            if value==0:assert r['identity'];continue
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            target=forward_points(field,lm)
            output=target*size;delta=output-pixels
            fixedDrift=np.linalg.norm(delta[:48],axis=1).max();assert fixedDrift<.75,(name,key,fixedDrift)
            if key in ['lipHeight','lipOffset']:
                drift=delta[48:66].mean(0)
                assert np.linalg.norm(delta[48:66]-drift,axis=1).max()<.4
                assert drift@(vertical if key=='lipHeight' else axis)*value*(-1 if key=='lipHeight' else 1)>0
            elif key=='lipTilt':
                angle=np.arctan2(np.cross(pixels[62]-pixels[58],output[62]-output[58]),np.dot(pixels[62]-pixels[58],output[62]-output[58]))
                assert angle*value>0
                assert np.linalg.norm((output[58]+output[62])*.5-center)<.4
                assert np.max(np.abs(np.linalg.norm(output[inner]-output[60],axis=1)-np.linalg.norm(pixels[inner]-pixels[60],axis=1)))<.4
            else:
                assert np.linalg.norm(delta[inner],axis=1).max()<.5
                if key=='lipWidth':
                    assert (np.linalg.norm(output[62]-output[58])-np.linalg.norm(pixels[62]-pixels[58]))*value>0
                    assert np.max(np.abs(delta[48:66]@vertical))<.3
                if key=='lipUpper':
                    assert delta[50]@vertical*value<0
                    assert np.linalg.norm(delta[53:58],axis=1).max()<.4
                if key=='lipLower':
                    assert delta[55]@vertical*value>0
                    assert np.linalg.norm(delta[48:53],axis=1).max()<.4
                if key=='lipCupid':
                    assert delta[50]@vertical*value>0 and delta[49]@vertical*value<0
                    assert np.linalg.norm(delta[[58,62]],axis=1).max()<.4
            records.append(dict(name=name,key=key,value=value,fixedDrift=float(fixedDrift),ms=r['milliseconds']))
    sheet.save(OUT/f'{name}-comparison.jpg')
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
print('Independent mouth objectives passed; inspect comparison images.')
