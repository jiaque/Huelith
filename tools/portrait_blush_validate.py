from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-blush';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());s=recipe['people'][0]['settings'];s['parameters']={};s['blushMaterialVersion']=2
size=list(Image.open(source).size);masks=[];frames=[]
for shape in ['underEye','horizontal','diagonal']:
 s['blushMaterial']=shape;s['parameters']={'makeup.blush.left':65,'makeup.blush.right':65}
 path=OUT/(shape+'-mask.png');call(op='portrait_geometry',recipe=recipe,size=size,styleMask='makeup.blush.left',mask=str(path));masks.append(np.asarray(Image.open(path)));assert masks[-1].max()>100
 call(op='portrait_geometry',recipe=recipe,source=str(source),size=size,render=str(OUT/(shape+'.png')));im=Image.open(OUT/(shape+'.png')).convert('RGB');x,y,w,h=recipe['people'][0]['face'];frames.append(im.crop((int(x*im.width),int(y*im.height),int((x+w)*im.width),int((y+h)*im.height))))
assert all(not np.array_equal(masks[i],masks[j]) for i in range(3) for j in range(i))
board=Image.new('RGB',(frames[0].width*3,frames[0].height))
for i,im in enumerate(frames):board.paste(im,(i*im.width,0))
board.save(OUT/'compare.png')
s['side']=0;s['linked']=False;s['parameters']={'makeup.blush.right':40};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=3);before=wait()['recipe'];call(op='portrait_state',click='portraitMaterialCard_blushMaterial_horizontal');after=wait()['recipe'];a=after['people'][0]['settings'];assert a['blushMaterial.left']=='horizontal' and a['parameters']['makeup.blush.left']==35 and a['parameters']['makeup.blush.right']==40
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
call(op='portrait_state',parameter='makeup.blushX',value=8);wait();call(op='portrait_state',parameter='makeup.blushY',value=-3);wait();call(op='portrait_state',parameter='makeup.blushSpread',value=25);after=wait()['recipe'];a=after['people'][0]['settings'];assert a['parameters']['makeup.blushX.left']==8 and a['parameters'].get('makeup.blushX.right',0)==0
before_drag=copy.deepcopy(after);call(op='portrait_state',click='portraitPlaceStyle');call(op='portrait_state',paintPath=[[.55,.30],[.56,.31],[.57,.32]]);after=wait()['recipe'];a=after['people'][0]['settings'];assert a['parameters']['makeup.blushX.left']>8 and a['parameters']['makeup.blushY.left']>-3
assert a['parameters'].get('makeup.blushX.right',0)==0
call(op='portrait_state',history=-1);assert wait()['recipe']==before_drag
call(op='portrait_state',history=1);assert wait()['recipe']==after
portable=call(op='portrait_geometry',portableSkin=a,makeupPreset=True,targetSettings={});assert portable['portable']['blushMaterial.left']=='horizontal' and portable['merged']['blushMaterialVersion']==2
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'blush.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close')
# Material finish is independent of pigment and retains alpha; switching it off restores pigment exactly.
recipe=after;s=recipe['people'][0]['settings'];s['parameters']={'makeup.blush.left':35,'makeup.blush.right':35};s['blushMaterialVersion']=2
s['cheekLighting']=call(op='portrait_geometry',source=str(source),person=recipe['people'][0],repairAnalysis=True)['cheekLighting'];assert s['cheekLighting']
outputs=[]
for name,extra in [('color',{}),('matte',{'makeup.blushGloss.left':-100}),('gloss',{'makeup.blushGloss.left':100}),('sparkle',{'makeup.blushSparkle.left':100})]:
 s['parameters']={'makeup.blush.left':35,'makeup.blush.right':35,**extra};out=OUT/name
 call(op='portrait_geometry',recipe=recipe,source=str(source),size=size,render=str(out.with_suffix('.png')),linearRender=str(out.with_suffix('.bin')));outputs.append(np.fromfile(out.with_suffix('.bin'),np.float32).reshape(size[1],size[0],4))
for output in outputs[1:]:assert np.array_equal(output[:,:,3],outputs[0][:,:,3]) and not np.array_equal(output,outputs[0])
assert np.min(outputs[1]-outputs[0])<0 and np.max(outputs[1]-outputs[0])<1e-6
assert np.min(outputs[2]-outputs[0])>-1e-6 and np.max(outputs[2]-outputs[0])>0
assert np.max(outputs[3]-outputs[0])>0
portable=call(op='portrait_geometry',portableSkin=s,makeupPreset=True,targetSettings={})['portable'];assert 'cheekLighting' not in portable
call(op='shutdown');print('Distinct cheek fields, side positioning, card activation, undo, preset, independent finish and saved output passed')
