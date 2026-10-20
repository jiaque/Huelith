"""Distinguish chin width from length and verify actual contour fairing."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-face-combinations';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
def bending(points):
    edges=np.diff(points[:17],axis=0);tangents=edges/np.linalg.norm(edges,axis=1)[:,None]
    return float(np.sum(np.diff(tangents,axis=0)**2))
for name in ['leroy-chiao','chris-hadfield','glasses','jessica-watkins','christina-koch','ellen-ochoa']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=np.array(Image.open(source).size)
    p=recipe['people'][0];p['settings']['contourVersion']=2
    lm=np.array(p['landmarks'])[:,:2];pixels=lm*size
    sheet=Image.new('RGB',(990,760),(30,34,38));draw=ImageDraw.Draw(sheet)
    for row,key in enumerate(['small','vShape']):
        for column,value in enumerate([0,50,100]):
            print('Rendering',name,key,value,flush=True)
            p['settings']['parameters']={'face.'+key:value};path=OUT/f'{name}-{key}-{value}.png'
            r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(path))
            x,y,w,h=p['face'];crop=Image.open(path).crop((int((x-w*.15)*size[0]),int(y*size[1]),int((x+w*1.15)*size[0]),int((y+h*1.15)*size[1])))
            crop.thumbnail((330,350));sheet.paste(crop,(column*330+(330-crop.width)//2,row*380+30));draw.text((column*330+4,row*380+5),f'{key} {value}',fill='white')
            if value==0:assert r['identity'];continue
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            output=forward_points(field,lm)*size;delta=output-pixels
            fixed=np.r_[0,16:66];drift=np.linalg.norm(delta[fixed],axis=1).max()
            assert drift<.75,(name,key,'fixed drift',drift)
            ratio=np.linalg.norm(output[9]-output[7])/np.linalg.norm(pixels[9]-pixels[7])
            assert ratio<1,(name,key,'chin contraction')
            jawRatio=np.linalg.norm(output[12]-output[4])/np.linalg.norm(pixels[12]-pixels[4])
            assert jawRatio<1,(name,key,'jaw contraction')
            # The combined control is exactly its published base targets, not
            # a second image resampling pass or a destructive parameter rewrite.
            originalField=field.copy()
            p['settings']['parameters']=({'face.width':-.35*value,'face.jaw':-.22*value,'face.chinWidth':-.12*value} if key=='small'
                else {'face.jaw':-.45*value,'face.chinWidth':-.30*value,'face.chin':.08*value})
            call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'expanded.bin'))
            expanded=np.fromfile(OUT/'expanded.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            assert np.max(np.abs(expanded-originalField))<1e-6,(name,key,'expanded contribution differs')
            records.append(dict(name=name,key=key,value=value,fixedDrift=float(drift),chinWidthRatio=float(ratio),bendingBefore=bending(pixels),bendingAfter=bending(output)))
    sheet.save(OUT/f'{name}-comparison.jpg')
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
print('Chin width and outline smoothing passed; inspect images.')
