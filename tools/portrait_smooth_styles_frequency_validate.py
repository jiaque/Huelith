from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-smooth-styles';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'frequency-settings'))
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/acne-face-recipe.json').read_text());p=recipe['people'][0];p['settings']['smoothVersion']=2;p['settings']['parameters']={'skin.smooth':100}
size=list(Image.open(ROOT/'docs/test-evidence/M3/quality/acne-face-source.png').size)
call(op='portrait_geometry',recipe=recipe,size=size,styleMask='skin.smooth',mask=str(OUT/'mask.png'))
mask=np.asarray(Image.open(OUT/'mask.png'))>245;assert mask.sum()>1000
xx=np.arange(size[0])[None,:];results=[]
for period in [3,32]:
    linear=np.broadcast_to(np.array([.34,.18,.12])[None,None,:]+.006*np.sin(xx*2*np.pi/period)[:,:,None],(size[1],size[0],3))
    srgb=1.055*linear**(1/2.4)-.055;source=OUT/f'frequency-{period}.png';Image.fromarray(np.uint8(np.clip(srgb*255,0,255))).save(source)
    data={}
    for style,value in [('none',0),('classic',100),('texture',100),('native',100)]:
        p['settings']['parameters']['skin.smooth']=value;p['settings']['smoothStyle']=style;path=OUT/f'frequency-{period}-{style}.bin'
        call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path.with_suffix('.png')),linearRender=str(path))
        rgba=np.fromfile(path,dtype=np.float32).reshape(size[1],size[0],4);data[style]=float(np.std((rgba[:,:,:3]@np.array([.2627,.678,.0593]))[mask]))
    ratios={style:v/data['none'] for style,v in data.items() if style!='none'};results.append(dict(period=period,ratios=ratios))
    assert ratios['classic']<ratios['texture'] and ratios['classic']<ratios['native'],ratios
    if period==32:assert ratios['native']>ratios['texture'],ratios
(OUT/'frequency-metrics.json').write_text(json.dumps(results,indent=2));call(op='shutdown');print(results)
