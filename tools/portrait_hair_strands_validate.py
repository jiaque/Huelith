from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-hair-strands';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+90
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());recipe=copy.deepcopy(real);p=recipe['people'][0];p['face']=[.15,.2,.7,.7];s=p['settings'];white=encoded(Image.new('L',(320,240),255));s['automaticMasks']={'hair':white};s['parameters']={'hair.zone_test':100};s['colors']={'hair.zone_test':[.2,.03,.01]};s['masks']={'hair.zone_test':white};s['hairColorVersion']=2;zone={'key':'hair.zone_test','name':'test','mode':'highlights','strandVersion':2,'strandWidth':20,'strandDensity':25};s['hairZones']=[zone]
yy,xx=np.mgrid[:240,:320];gray=(80+25*np.sin((xx-.0015*(yy-120)**2)*.4)).astype('uint8');source=OUT/'curved-hair.png';Image.fromarray(np.repeat(gray[:,:,None],3,axis=2)).save(source)
s['hairLighting']=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True)['hairLighting'];curves=s['hairLighting']['strands'];assert len(curves)>20
errors=[];changes=[]
for curve in curves:
 pts=np.array(curve['points'])*[320,240];dx=np.diff(pts[:,0]);dy=np.diff(pts[:,1]);mid=(pts[:-1,1]+pts[1:,1])*.5;good=abs(dy)>.1
 errors.extend(abs(dx[good]/dy[good]-.003*(mid[good]-120)));changes.append(np.ptp(pts[:,0]))
assert np.median(errors)<.12 and np.median(changes)>2,(np.median(errors),np.median(changes))
def mask(name):
 path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=[320,240],styleMask='hair.zone_test',mask=str(path));return np.asarray(Image.open(path))
a=mask('sparse');assert a.max()>0;assert np.array_equal(a,mask('repeat'));zone['strandDensity']=75;b=mask('dense');assert b.sum()>a.sum();zone['strandWidth']=80;c=mask('wide');assert c.sum()>b.sum()
flat=OUT/'flat.png';Image.new('RGB',(320,240),(80,80,80)).save(flat);assert call(op='portrait_geometry',source=str(flat),person=p,repairAnalysis=True)['hairLighting']['strands']==[]
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=real;s=recipe['people'][0]['settings'];s['parameters']={'hair.zone_test':70};s['hairColorVersion']=2;s['hairZones']=[{'key':'hair.zone_test','name':'test','mode':'solid'}];s['masks']={'hair.zone_test':white};s['colors']={'hair.zone_test':[.15,.045,.02]}
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=12);wait();call(op='portrait_state',styleSection=0);before=wait()['recipe'];call(op='portrait_state',hairZoneMode='highlights');after=wait()['recipe'];assert after['people'][0]['settings']['hairZones'][0]['strandVersion']==2
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);wait();call(op='portrait_state',strandWidth=55);wait();call(op='portrait_state',strandDensity=60);after=wait()['recipe'];zone=after['people'][0]['settings']['hairZones'][0];assert zone['strandWidth']==55 and zone['strandDensity']==60
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'strands.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Curved direction following, independent width/density, deterministic paths, low-confidence rejection, undo and saving passed')
