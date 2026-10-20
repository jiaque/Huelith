from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import base64,io
OUT=ROOT/'docs/test-evidence/M3/semantic-teeth-color';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=30);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
records=[]
for name in ['leroy-chiao','jessica-watkins','ellen-ochoa']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=list(Image.open(source).size);recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={};p['settings']['automaticMasks'].pop('teeth',None)
    call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=10);state=wait();assert 'teeth' in state['recipe']['people'][0]['settings']['automaticMasks']
    for key in ['teeth.yellow','teeth.light']:
        before=wait()['recipe'];call(op='portrait_state',parameter=key,value=50);state=wait();assert state['recipe']['people'][0]['settings']['teethColorVersion']==2
        call(op='portrait_state',history=-1);assert wait()['recipe']==before
        call(op='portrait_state',history=1);wait();call(op='portrait_state',doubleClickParameter=key);wait()
    call(op='portrait_state',parameter='teeth.yellow',value=60);wait();call(op='portrait_state',parameter='teeth.light',value=40);state=wait()
    saved=call(op='portrait_state',source=str(source),render=str(OUT/f'{name}-workflow.png'),checkRegion=True,save=str(OUT/f'{name}.huelith'));assert saved['roundtrip'] and saved['regionError']<2e-6
    recipe=state['recipe'];p=recipe['people'][0];call(op='portrait_close');x,y,w,h=p['face'];bounds=(int(x*size[0]),int((y+h*.55)*size[1]),int((x+w)*size[0]),int((y+h)*size[1]));cw,ch=bounds[2]-bounds[0],bounds[3]-bounds[1];sheet=Image.new('RGB',(cw*3,ch+30),(30,34,38));draw=ImageDraw.Draw(sheet)
    for col,key in enumerate(['none','teeth.yellow','teeth.light']):
        p['settings']['parameters']={} if key=='none' else {key:100};path=OUT/f'{name}-{key}.png';r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path),colorMetrics=True)
        if key=='teeth.yellow':assert r['maximumLuminanceError']<2e-6,r
        sheet.paste(Image.open(path).crop(bounds),(col*cw,30));draw.text((col*cw+4,5),key,fill='white');records.append(dict(name=name,key=key,**r))
    sheet.save(OUT/f'{name}-comparison.png')
# Isolate the color transform from segmentation using an explicit user mask.
buf=io.BytesIO();Image.new('L',(32,32),255).save(buf,format='PNG');encoded=base64.b64encode(buf.getvalue()).decode();p['settings']['masks']={k:encoded for k in ['teeth.yellow','teeth.light']};size=[256,16]
for kind,key in [('yellow','teeth.yellow'),('neutral','teeth.yellow'),('ramp','teeth.light')]:
    path=OUT/f'{kind}.png'
    if kind=='ramp':Image.fromarray(np.tile(np.arange(256,dtype=np.uint8)[None,:,None],(16,1,3))).save(path)
    else:Image.new('RGB',size,(220,210,170) if kind=='yellow' else (190,190,190)).save(path)
    outputs=[]
    for amount in [0,100]:
        p['settings']['parameters']={key:amount};binary=OUT/f'{kind}-{amount}.bin';r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(path),render=str(binary.with_suffix('.png')),linearRender=str(binary),colorMetrics=True);outputs.append(np.fromfile(binary,np.float32).reshape(16,256,4))
        if key=='teeth.yellow':assert r['maximumLuminanceError']<2e-6,r
    a,b=outputs
    if kind=='neutral':assert np.max(abs(b-a))<2e-6
    if kind=='yellow':assert b[0,0,2]>a[0,0,2] and b[0,0,2]<b[0,0,0]
    if kind=='ramp':
        la=a[0,:,:3]@np.array([.2627,.678,.0593]);lb=b[0,:,:3]@np.array([.2627,.678,.0593]);assert np.diff(lb).min()>=-1e-6 and lb.max()<=1+2e-6 and lb[128]>la[128] and abs(lb[-1]-la[-1])<2e-6
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print('Teeth yellow axis, preserved brightness, neutral colors, highlight curve and saved workflow passed.')
