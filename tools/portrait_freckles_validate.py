from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-freckles';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text(encoding='utf-8'));recipe=copy.deepcopy(real);p=recipe['people'][0];s=p['settings'];s['parameters']={'makeup.freckles':100,'makeup.frecklesSize':40,'makeup.frecklesDensity':20};s['masks']={};s['automaticMasks']={'skin':encoded(Image.new('L',(512,512),255))};lm=np.array(p['landmarks'])[:,:2];lm=(lm-lm.mean(axis=0))*.65/(lm.max(axis=0)-lm.min(axis=0)).max()+.5;p['landmarks']=lm.tolist()
def mask(name):
 path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=[512,512],styleMask='makeup.freckles',mask=str(path));return np.array(Image.open(path))
a=mask('sparse');assert a.max()>80 and 0<np.count_nonzero(a)<5000;assert np.array_equal(a,mask('repeat'))
s['parameters']['makeup.frecklesDensity']=80;b=mask('dense');assert np.all(b>=a) and b.sum()>a.sum()*1.5
s['freckleSeed']=9137;c=mask('new-seed');assert not np.array_equal(b,c);s.pop('freckleSeed')
p['landmarks']=np.column_stack([1-lm[:,1],lm[:,0]]).tolist();d=mask('rotated');assert np.abs(np.rot90(b,-1).astype(float)-d).mean()<.02;p['landmarks']=lm.tolist()
s['automaticMasks']['eyeglassFrame']=encoded(Image.new('L',(512,512),255));assert mask('occluded').max()==0;s['automaticMasks'].pop('eyeglassFrame')
s['masks']={'makeup.freckles':encoded(Image.new('L',(512,512),0))};assert mask('erased').max()==0;s['masks']={}
for first,last in [(36,42),(42,48),(48,60)]:
 center=(lm[first:last].mean(axis=0)*512).astype(int);assert b[center[1],center[0]]==0
# UI: apply, density without relocation, seed shuffle/undo, palette and project persistence.
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=copy.deepcopy(real);recipe['people'][0]['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=8);before=wait()['recipe'];call(op='portrait_state',click='portraitFrecklesApply');applied=wait()['recipe'];assert applied['people'][0]['settings']['parameters']['makeup.freckles']==65
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();call(op='portrait_state',click='portraitFrecklesShuffle');shuffled=wait()['recipe'];assert shuffled['people'][0]['settings']['freckleSeed']!=1729
call(op='portrait_state',history=-1);assert wait()['recipe']==applied;call(op='portrait_state',history=1);wait();call(op='portrait_state',click='portraitFreckleColor_694332');after=wait()['recipe']
portable=call(op='portrait_geometry',portableSkin=after['people'][0]['settings'],makeupPreset=True,targetSettings={});assert portable['merged']['freckleSeed']==after['people'][0]['settings']['freckleSeed'] and 'makeup.freckles' in portable['portable']['colors']
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'freckles.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'workflow.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Freckles retain existing points with density, repeat/rotate, exclude eyes/mouth/frames, erase, apply/shuffle undo, color, preset seed, ROI and reopen passed')
