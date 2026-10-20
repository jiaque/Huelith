"""Head scale must transport face and scalp together and leave shoulders fixed."""
from pathlib import Path
import base64,io
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-head';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['leroy-chiao','chris-hadfield','glasses','jessica-watkins','christina-koch','ellen-ochoa']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=np.array(Image.open(source).size)
    recipe=call(op='portrait_geometry',headAnalysis=True,recipe=recipe,mask=str(OUT/f'{name}-head.png'))['recipe']
    p=recipe['people'][0];p['settings']['headGeometryVersion']=2
    lm=np.array(p['landmarks'])[:,:2];pixels=lm*(size-1)
    axis=pixels[42:48].mean(0)-pixels[36:42].mean(0);axis/=np.linalg.norm(axis);vertical=np.array([-axis[1],axis[0]])
    pivot=pixels[8]+vertical*np.linalg.norm(pixels[16]-pixels[0])*.03
    sheet=Image.new('RGB',(1200,520),(30,34,38));draw=ImageDraw.Draw(sheet)
    for column,value in enumerate([-100,0,100]):
        print('Rendering',name,value,flush=True)
        p['settings']['parameters']={'head.size':value};path=OUT/f'{name}-{value}.png'
        r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(path))
        x,y,w,h=p['face'];crop=Image.open(path).crop((int((x-w*.5)*size[0]),int((y-h*.6)*size[1]),int((x+w*1.5)*size[0]),int((y+h*1.65)*size[1])))
        crop.thumbnail((400,490));sheet.paste(crop,(column*400+(400-crop.width)//2,30));draw.text((column*400+4,5),f'head.size {value}',fill='white')
        if value==0:assert r['identity'];continue
        mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
        output=forward_points(field,lm)*(size-1);expected=pivot+(pixels-pivot)*(1+.0008*value)
        featureError=float(np.linalg.norm(output-expected,axis=1).max());assert featureError<.75,(name,'feature scale',featureError)
        mask=np.asarray(Image.open(OUT/f'{name}-head.png'));gy,gx=np.mgrid[0:mask.shape[0]:9,0:mask.shape[1]:9];keep=mask[gy,gx]>127
        headPoints=np.column_stack([(gx[keep]+.5)/mask.shape[1],(gy[keep]+.5)/mask.shape[0]])
        headPixels=headPoints*(size-1);headOutput=forward_points(field,headPoints)*(size-1)
        headError=float(np.linalg.norm(headOutput-(pivot+(headPixels-pivot)*(1+.0008*value)),axis=1).max())
        assert headError<1.5,(name,'scalp affine error',headError)
        pose=np.array(p['settings']['pose']);shoulders=pose[[11,12],:2]
        reliable=(pose[[11,12],2]>.5)&(((shoulders*(size-1)-pivot)@vertical)>np.linalg.norm(pixels[16]-pixels[0])*.08)
        shoulders=shoulders[reliable]
        shoulderError=float(np.linalg.norm(sample(field,shoulders)*size,axis=1).max()) if len(shoulders) else 0.
        assert shoulderError<.75,(name,'shoulder drift',shoulderError)
        body=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['bodyMask']))));lineError=0.
        for line in p['settings'].get('backgroundLines',[]):
            a=np.array(line[:2]);b=np.array(line[2:]);q=a+(b-a)*np.linspace(0,1,150)[:,None]
            indices=np.clip((q*[body.shape[1]-1,body.shape[0]-1]).astype(int),0,[body.shape[1]-1,body.shape[0]-1])
            q=q[body[indices[:,1],indices[:,0]]<25]
            if len(q):lineError=max(lineError,float(np.linalg.norm(sample(field,q)*size,axis=1).max()))
        assert lineError<1.,(name,'background line',lineError)
        records.append(dict(name=name,value=value,featureError=featureError,headError=headError,shoulderError=shoulderError,verifiedShoulders=len(shoulders),backgroundLineError=lineError,ms=r['milliseconds']))
    sheet.save(OUT/f'{name}-comparison.jpg')
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
print('Head island and shoulder invariants passed; inspect images.')
