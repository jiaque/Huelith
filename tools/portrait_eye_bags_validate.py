from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-eye-bags';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=30);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
records=[]
for name in ['glasses','jessica-watkins','ellen-ochoa','leroy-chiao']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=list(Image.open(source).size);recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={}
    for key in list(p['settings']['automaticMasks']):
        if key.startswith('eye.') or key=='teeth':del p['settings']['automaticMasks'][key]
    call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=9);state=wait()
    assert 'eye.bags' in state['recipe']['people'][0]['settings']['automaticMasks']
    before=state['recipe'];call(op='portrait_state',parameter='eye.bags',value=70);state=wait();assert state['recipe']['people'][0]['settings']['eyeBagsVersion']==2
    call(op='portrait_state',history=-1);assert wait()['recipe']==before
    call(op='portrait_state',history=1);state=wait()
    saved=call(op='portrait_state',source=str(source),render=str(OUT/f'{name}-workflow.png'),checkRegion=True,save=str(OUT/f'{name}.huelith'));assert saved['roundtrip'] and saved['regionError']<2e-6,saved
    recipe=state['recipe'];p=recipe['people'][0];call(op='portrait_close');(OUT/f'{name}-recipe.json').write_text(json.dumps(recipe))
    x,y,w,h=p['face'];bounds=(int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1]));cw,ch=bounds[2]-bounds[0],bounds[3]-bounds[1];sheet=Image.new('RGB',(cw*3,ch+30),(30,34,38));draw=ImageDraw.Draw(sheet)
    for col,(version,value) in enumerate([(2,0),(1,100),(2,100)]):
        p['settings']['eyeBagsVersion']=version;p['settings']['parameters']={'eye.bags':value};path=OUT/f'{name}-v{version}-{value}.png';r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path),colorMetrics=True)
        sheet.paste(Image.open(path).crop(bounds),(col*cw,30));draw.text((col*cw+4,5),f'eye bags v{version} {value}',fill='white');records.append(dict(name=name,version=version,value=value,**r))
    sheet.save(OUT/f'{name}-comparison.png')
mask=OUT/'under.png';call(op='portrait_geometry',recipe=recipe,size=size,styleMask='eye.bags',mask=str(mask));m=np.asarray(Image.open(mask));cy,cx=np.unravel_index(np.argmax(m),m.shape);yy,xx=np.mgrid[:size[1],:size[0]];radius=p['face'][2]*size[0]*.04
linear=np.array([.34,.18,.12])[None,None,:]*(1+.12*np.sin((yy-cy)*2*np.pi/max(4,radius*2.5))[:,:,None]);srgb=1.055*linear**(1/2.4)-.055;path=OUT/'contrast.png';Image.fromarray(np.uint8(srgb*255)).save(path);renders=[]
for value in [0,100]:
    p['settings']['parameters']={'eye.bags':value};binary=OUT/f'contrast-{value}.bin';call(op='portrait_geometry',recipe=recipe,size=size,source=str(path),render=str(binary.with_suffix('.png')),linearRender=str(binary));renders.append(np.fromfile(binary,np.float32).reshape(size[1],size[0],4)[:,:,:3])
a,b=renders;error=np.max(abs(a/np.maximum(a.sum(axis=2,keepdims=True),1e-8)-b/np.maximum(b.sum(axis=2,keepdims=True),1e-8)));assert error<2e-6,error
lum=lambda rgb:rgb@np.array([.2627,.678,.0593]);ratio=np.std(lum(b)[m>150])/np.std(lum(a)[m>150]);assert ratio<.99,ratio
records.append(dict(kind='neutral-contrast',contrastRatio=float(ratio),chromaError=float(error)))
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print('Eye bag contrast correction, chroma preservation, undo and saved regions passed.',ratio)
