from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-accessory-mask';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());recipe=copy.deepcopy(real);p=recipe['people'][0];p['face']=[.2,.3,.5,.5];s=p['settings'];s['parameters']={'hair.accessory':100,'hair.accessorySize':50};s['hairAccessoryMaskAnchor']=copy.deepcopy(s['parameters']);s['hairAccessoryMaskFollows']=True;mask=Image.new('L',(512,512),255);ImageDraw.Draw(mask).ellipse((326,111,334,119),fill=0);s['masks']={'hair.accessory':encoded(mask)}
def render(name):
 path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=[512,512],styleMask='hair.accessory',mask=str(path));return np.asarray(Image.open(path))
a=render('initial');assert a[115,330]<20 and a[115,310]>200
s['parameters']['hair.accessoryX']=25;b=render('moved');assert b[115,394]<20 and b[115,374]>200
s['parameters']['hair.accessoryAngle']=50;c=render('rotated');assert c[125,384]<40 and c[105,384]>200
s['hairAccessoryMaskFollows']=False;d=render('fixed');assert d[125,384]>200
recipe=real;s=recipe['people'][0]['settings'];s['parameters']={'hair.accessory':100,'hair.accessorySize':55};source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=12);wait();call(op='portrait_state',styleSection=3);wait();call(op='portrait_state',rangeKey='hair.accessory',rangeMode=2);call(op='portrait_state',paintPath=[[.6,.18],[.61,.18]]);painted=wait()['recipe'];assert painted['people'][0]['settings']['hairAccessoryMaskFollows'] and 'hairAccessoryMaskAnchor' in painted['people'][0]['settings']
call(op='portrait_state',parameter='hair.accessoryX',value=30);moved=wait()['recipe'];call(op='portrait_state',history=-1);assert wait()['recipe']==painted
call(op='portrait_state',history=1);assert wait()['recipe']==moved
# Exercise real canvas scale/rotate handles using source pixel coordinates.
w,h=Image.open(source).size;f=np.array(moved['people'][0]['face']);params=moved['people'][0]['settings']['parameters'];center=np.array([(f[0]+f[2]*(.85+params.get('hair.accessoryX',0)*.01))*w,(f[1]+f[3]*(-.15+params.get('hair.accessoryY',0)*.01))*h]);scale=f[2]*w*(.12+params['hair.accessorySize']*.003)
call(op='portrait_state',click='portraitPlaceStyle');start=(center+np.array([1.1,.9])*scale)/[w,h];end=(center+np.array([1.1,.9])*scale*1.15)/[w,h];call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);scaled=wait()['recipe'];assert scaled['people'][0]['settings']['parameters']['hair.accessorySize']>65
call(op='portrait_state',history=-1);assert wait()['recipe']==moved
call(op='portrait_state',click='portraitPlaceStyle');start=(center+np.array([0,-1.2])*scale)/[w,h];end=(center+np.array([.6,-1.2*np.sqrt(3)/2])*scale)/[w,h];call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);rotated=wait()['recipe'];assert abs(rotated['people'][0]['settings']['parameters']['hair.accessoryAngle']-30/1.8)<1
call(op='portrait_state',history=-1);assert wait()['recipe']==moved
call(op='portrait_state',source=str(source),render=str(OUT/'before-switch.png'));call(op='portrait_state',click='portraitAccessoryMaskFollows');after=wait()['recipe'];assert not after['people'][0]['settings']['hairAccessoryMaskFollows'];call(op='portrait_state',source=str(source),render=str(OUT/'after-switch.png'));a=np.asarray(Image.open(OUT/'before-switch.png')).astype(float);b=np.asarray(Image.open(OUT/'after-switch.png')).astype(float);assert abs(a-b).mean()<.1
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'accessory.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');fresh=copy.deepcopy(real);fresh['people'][0]['settings']['parameters']={'hair.accessory':100};fresh['people'][0]['settings']['masks']={};fresh['people'][0]['settings'].pop('hairAccessoryMaskFollows',None);call(op='portrait_window',path=str(source),recipe=fresh);wait();call(op='portrait_state',select=0,panel=12);wait();call(op='portrait_state',styleSection=3);call(op='portrait_state',click='portraitAccessoryMaskFollows');wait();call(op='portrait_state',rangeKey='hair.accessory',rangeMode=2,paintPath=[[.6,.18],[.61,.18]]);assert not wait()['recipe']['people'][0]['settings']['hairAccessoryMaskFollows']
call(op='portrait_close');call(op='shutdown');print('Accessory erasure follows translation/rotation; fixed mode, rebasing, undo and saving passed')
