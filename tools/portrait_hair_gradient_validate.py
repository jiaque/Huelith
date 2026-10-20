from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-hair-gradient';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());recipe=copy.deepcopy(real);s=recipe['people'][0]['settings'];white=encoded(Image.new('L',(320,240),255));s['automaticMasks']={'hair':white};s['hairColorVersion']=2;s['parameters']={'hair.zone_test':100};s['masks']={'hair.zone_test':white};s['colors']={'hair.zone_test':[.22,.035,.018]};zone={'key':'hair.zone_test','name':'Test','mode':'gradient','secondColor':[.02,.06,.22]};s['hairZones']=[zone];source=OUT/'gray.png';Image.new('RGB',(320,240),(100,100,100)).save(source);frames=[]
for name,axis in [('horizontal',[.1,.5,.9,.5]),('vertical',[.5,.1,.5,.9]),('reverse',[.9,.5,.1,.5])]:
 zone['gradientAxis']=axis;out=OUT/name;call(op='portrait_geometry',recipe=recipe,source=str(source),size=[320,240],render=str(out.with_suffix('.png')));frames.append(np.asarray(Image.open(out.with_suffix('.png'))))
a,b,c=frames;assert a[120,50,0]>a[120,270,0] and a[120,50,2]<a[120,270,2];assert b[40,160,0]>b[200,160,0];assert np.array_equal(a[:,50],c[:,269])
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=real;s=recipe['people'][0]['settings'];s['hairColorVersion']=2;s['parameters']={'hair.zone_test':80};s['hairZones']=[{'key':'hair.zone_test','name':'Test','mode':'gradient','gradientAxis':[.35,.17,.65,.27],'secondColor':[.03,.04,.18]}];s['colors']={'hair.zone_test':[.18,.045,.025]};s['masks']={'hair.zone_test':white}
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=12);wait();call(op='portrait_state',styleSection=0);call(op='portrait_state',click='portraitHairGradientAxis');before=wait()['recipe'];call(op='portrait_state',paintPath=[[.35,.17],[.40,.20]]);after=wait()['recipe'];z=after['people'][0]['settings']['hairZones'][0];assert abs(z['gradientAxis'][0]-.4)<.005 and abs(z['gradientAxis'][1]-.2)<.005 and z['gradientAxis'][2:]==[.65,.27]
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
call(op='portrait_state',click='portraitHairGradientAxis');call(op='portrait_state',paintPath=[[.65,.27],[.60,.3]],paintEnd='ungrab');wait();call(op='portrait_state',paintPath=[[.1,.1],[.15,.15]]);after=wait()['recipe'];z=after['people'][0]['settings']['hairZones'][0];assert abs(z['gradientAxis'][0]-.1)<.005 and abs(z['gradientAxis'][2]-.15)<.005
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'gradient.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Gradient axis projection, endpoint dragging, reversal, capture loss, undo and saved output passed')
