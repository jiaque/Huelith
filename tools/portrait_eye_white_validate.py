from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-eye-white';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'settings'))
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
    assert 'eye.white' in state['recipe']['people'][0]['settings']['automaticMasks']
    before=state['recipe'];call(op='portrait_state',parameter='eye.white',value=70);state=wait();assert state['recipe']['people'][0]['settings']['eyeWhiteVersion']==2
    call(op='portrait_state',history=-1);assert wait()['recipe']==before
    call(op='portrait_state',history=1);state=wait()
    saved=call(op='portrait_state',source=str(source),render=str(OUT/f'{name}-workflow.png'),checkRegion=True,save=str(OUT/f'{name}.huelith'));assert saved['roundtrip'] and saved['regionError']<2e-6,saved
    recipe=state['recipe'];p=recipe['people'][0];call(op='portrait_close');(OUT/f'{name}-recipe.json').write_text(json.dumps(recipe))
    x,y,w,h=p['face'];bounds=(int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1]));cw,ch=bounds[2]-bounds[0],bounds[3]-bounds[1];sheet=Image.new('RGB',(cw*3,ch+30),(30,34,38));draw=ImageDraw.Draw(sheet)
    for col,(version,value) in enumerate([(2,0),(1,100),(2,100)]):
        p['settings']['eyeWhiteVersion']=version;p['settings']['parameters']={'eye.white':value};path=OUT/f'{name}-v{version}-{value}.png';r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path),colorMetrics=True)
        sheet.paste(Image.open(path).crop(bounds),(col*cw,30));draw.text((col*cw+4,5),f'eye white v{version} {value}',fill='white');records.append(dict(name=name,version=version,value=value,**r))
    sheet.save(OUT/f'{name}-comparison.png')
    mask_path=OUT/f'{name}-mask.png';call(op='portrait_geometry',recipe=recipe,size=size,styleMask='eye.white',mask=str(mask_path));mask=np.asarray(Image.open(mask_path));photo=np.asarray(Image.open(source).convert('RGB')).copy();tint=np.zeros_like(photo);tint[:,:,1]=255;alpha=mask[:,:,None]/255.*.7;overlay=Image.fromarray(np.uint8(photo*(1-alpha)+tint*alpha));crop=overlay.crop(bounds);crop.resize((cw*2,ch*2)).save(OUT/f'{name}-regions.png')
    if name in ['jessica-watkins','ellen-ochoa']:assert (mask>80).sum()>5,(name,(mask>80).sum())
    for start in [36,42]:
        center=(np.array(p['landmarks'][start][:2])+np.array(p['landmarks'][start+3][:2]))*.5
        assert mask[int(center[1]*size[1]),int(center[0]*size[0])]<30,(name,center)

(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print('Sclera preparation, pupil centers excluded, highlight-limited rendering and saved workflow passed.')
