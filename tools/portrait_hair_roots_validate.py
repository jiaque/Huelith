from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-hair-roots';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());recipe=copy.deepcopy(real);p=recipe['people'][0];p['face']=[.2,.25,.6,.65];s=p['settings'];s['parameters']={'hair.roots':100,'hair.rootWidth':30};s['hairColorVersion']=2;s['colors']={'hair.roots':[.22,.04,.02]};s['scalpGeometry']={'valid':True,'hairline':[[.15,.5],[.85,.5]]};hair=Image.new('L',(320,240),0);ImageDraw.Draw(hair).rectangle((20,20,299,119),fill=255);s['automaticMasks']={'hair':encoded(hair)}
source=OUT/'controlled.png';Image.new('RGB',(320,240),(100,100,100)).save(source)
def mask(name):
 path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=[320,240],styleMask='hair.roots',mask=str(path));return np.asarray(Image.open(path))
a=mask('narrow');assert a[115,160]>0 and a[40,160]==0 and a[125,160]==0
s['parameters']['hair.rootWidth']=90;b=mask('wide');assert b.sum()>a.sum() and not b[120:].any() and not b[:40].any()
s['scalpGeometry']['valid']=False;assert not mask('invalid').any()
recipe=real;s=recipe['people'][0]['settings'];s['parameters']={};source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=12);wait();call(op='portrait_state',styleSection=0);before=wait()['recipe'];call(op='portrait_state',parameter='hair.roots',value=80);after=wait()['recipe'];assert after['people'][0]['settings']['scalpGeometry']['valid'];assert after['people'][0]['settings']['hairColorVersion']==2
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);wait();call(op='portrait_state',parameter='hair.rootWidth',value=65);after=wait()['recipe']
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'roots.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Root band follows hairline, rejects skin/long hair/invalid detection; width, undo, region and saving passed')
