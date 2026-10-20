from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-hair-oil';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());recipe=copy.deepcopy(real);p=recipe['people'][0];s=p['settings'];s['parameters']={};mask=Image.new('L',(512,512),0);ImageDraw.Draw(mask).rectangle((24,24,487,487),fill=255);s['automaticMasks']={'hair':encoded(mask)}
yy,xx=np.mgrid[:512,:512];gray=75+12*np.sin(xx*.4)+150*np.exp(-((yy-180)/8)**2);gray=np.clip(gray,0,255).astype('uint8');source=OUT/'controlled.png';Image.fromarray(np.repeat(gray[:,:,None],3,axis=2)).save(source)
s['hairLighting']=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True)['hairLighting']
def render(name,strength):
 s['parameters']={'hair.oil':strength};out=OUT/name;call(op='portrait_geometry',recipe=recipe,source=str(source),size=[512,512],render=str(out.with_suffix('.png')),linearRender=str(out.with_suffix('.bin')));return np.fromfile(out.with_suffix('.bin'),np.float32).reshape(512,512,4)
a=render('before',0);b=render('after',100);area=np.s_[40:470,40:470,0]
delta=a[:,:,0]-b[:,:,0];assert delta[176:184,60:450].mean()>.01;assert delta[300:400,60:450].mean()<.002
assert np.all(b[:,:,:3]<=a[:,:,:3]+1e-6) and np.all(b[:,:,:3]>=a[:,:,:3]*.7-1e-6)
assert np.array_equal(a[:24],b[:24]) and np.array_equal(a[:,:,3],b[:,:,3]);assert np.isfinite(b).all()
s['masks']={'hair.oil':encoded(Image.new('L',(512,512),0))};c=render('erased',100);assert np.array_equal(a,c)
# A broad diffuse illumination gradient must not be treated as oil.
s['masks']={};gradient=np.tile(np.linspace(65,140,512,dtype=np.uint8),(512,1));Image.fromarray(np.repeat(gradient[:,:,None],3,axis=2)).save(source)
s['hairLighting']=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True)['hairLighting'];diffuse0=render('diffuse-before',0);diffuse1=render('diffuse-after',100);assert np.array_equal(diffuse0[80:430,80:430],diffuse1[80:430,80:430])
recipe=real;s=recipe['people'][0]['settings'];s['parameters']={};source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=12);wait();call(op='portrait_state',styleSection=1);before=wait()['recipe'];call(op='portrait_state',parameter='hair.oil',value=80);after=wait()['recipe'];assert after['people'][0]['settings']['parameters']['hair.oil']==80
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);wait();call(op='portrait_state',source=str(source),render=str(OUT/'oil-only.png'));call(op='portrait_state',parameter='head.hairVolume',value=20);after=wait()['recipe'];assert after['people'][0]['settings']['parameters']['head.hairVolume']==20
call(op='portrait_state',panel=2);wait();assert call(op='portrait_state')['recipe']['people'][0]['settings']['parameters']['head.hairVolume']==20
portable=call(op='portrait_geometry',portableSkin=after['people'][0]['settings'],hairPreset=True,targetSettings={})['portable'];assert portable['parameters']['head.hairVolume']==20 and portable['parameters']['hair.oil']==80;assert 'hairLighting' not in portable and 'scalpGeometry' not in portable
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'smooth.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Hair oil reduction confines correction to excess reflection; bounds, erasure, undo, region and saving passed')
