from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-texture';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['acne-face','jessica-watkins']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=list(Image.open(source).size);recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0];p['settings']['textureVersion']=2
    x,y,w,h=p['face'];bounds=(int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1]));cw,ch=bounds[2]-bounds[0],bounds[3]-bounds[1];sheet=Image.new('RGB',(cw*3,ch+30),(30,34,38));draw=ImageDraw.Draw(sheet)
    baseline=None
    for col,value in enumerate([0,-100,100]):
        p['settings']['parameters']={'skin.texture':value};path=OUT/f'{name}-{value}.png';binary=path.with_suffix('.bin');r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path),linearRender=str(binary));data=np.fromfile(binary,np.float32).reshape(size[1],size[0],4)[:,:,:3]
        if value==0:baseline=data
        else:
            a=baseline/np.maximum(baseline.sum(axis=2,keepdims=True),1e-8);b=data/np.maximum(data.sum(axis=2,keepdims=True),1e-8);error=np.max(abs(a-b));assert error<2e-6,error
            records.append(dict(name=name,value=value,chromaError=float(error)))
        sheet.paste(Image.open(path).crop(bounds),(col*cw,30));draw.text((col*cw+4,5),f'texture {value}',fill='white')
    sheet.save(OUT/f'{name}-comparison.png')
# Noise and structured detail are separate controlled inputs.
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/acne-face-recipe.json').read_text());p=recipe['people'][0];p['settings']['textureVersion']=2
size=list(Image.open(ROOT/'docs/test-evidence/M3/quality/acne-face-source.png').size);scale=768/max(size);ms=[round(v*scale) for v in size]
call(op='portrait_geometry',recipe=recipe,size=ms,styleMask='skin.texture',mask=str(OUT/'mask.png'));small=np.asarray(Image.open(OUT/'mask.png'));mask=small[(np.arange(size[1])*ms[1]/size[1]).astype(int)[:,None],(np.arange(size[0])*ms[0]/size[0]).astype(int)[None,:]]>240
for kind in ['noise','detail']:
    detail=np.random.default_rng(41).normal(0,.006,(size[1],size[0])) if kind=='noise' else np.broadcast_to(.02*np.sin(np.arange(size[0])[None,:]*2*np.pi/4),(size[1],size[0]))
    linear=np.array([.34,.18,.12])[None,None,:]+detail[:,:,None];srgb=1.055*linear**(1/2.4)-.055;source=OUT/f'{kind}.png';Image.fromarray(np.uint8(srgb*255)).save(source);results=[]
    for value in [0,100]:
        p['settings']['parameters']={'skin.texture':value};path=OUT/f'{kind}-{value}.bin';call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path.with_suffix('.png')),linearRender=str(path));rgb=np.fromfile(path,np.float32).reshape(size[1],size[0],4)[:,:,:3];results.append((rgb@np.array([.2627,.678,.0593]))[mask])
    gain=np.std(results[1])/np.std(results[0]);records.append(dict(kind=kind,contrastGain=float(gain)))
    if kind=='noise':assert gain<1.10,gain
    else:assert 1.1<gain<1.6,gain
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(records)
