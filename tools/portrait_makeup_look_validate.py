from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-makeup-look';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());s=recipe['people'][0]['settings'];s['parameters']={'skin.smooth':28,'makeup.blushX.left':4,'makeup.blushY.left':-2};s['enabled']={'skin':False};s['disabledParameters']={'skin.smooth':True};s['blushMaterialVersion']=2;s['hairColorVersion']=2
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=5);before=wait()['recipe'];frames=[]
for style in range(3):
 call(op='portrait_state',click=f'portraitMakeupLook{style}');after=wait()['recipe'];a=after['people'][0]['settings'];assert a['hairColorVersion']==2;assert a['enabled']['skin']==False and a['disabledParameters']['skin.smooth'] and a['parameters']['skin.smooth']==28
 assert a['parameters']['makeup.blushX.left']==4 and a['parameters']['makeup.blushY.left']==-2
 assert a['parameters']['makeup.lip']>0 and a['parameters']['makeup.lash.left']>0
 call(op='portrait_state',source=str(source),render=str(OUT/f'look-{style}.png'));frames.append(np.asarray(Image.open(OUT/f'look-{style}.png')))
 call(op='portrait_state',history=-1);assert wait()['recipe']==before
 call(op='portrait_state',history=1);assert wait()['recipe']==after
 call(op='portrait_state',history=-1);assert wait()['recipe']==before
assert all(not np.array_equal(frames[i],frames[j]) for i in range(3) for j in range(i))
call(op='portrait_state',click='portraitMakeupLook1');after=wait()['recipe'];a=copy.deepcopy(after['people'][0]['settings']);call(op='portrait_state',styleSection=0);call(op='portrait_state',parameter='makeup.lip',value=60);edited=wait()['recipe'];b=edited['people'][0]['settings'];assert b['parameters']['makeup.lip']==60
for key,value in a['parameters'].items():
 if key!='makeup.lip':assert b['parameters'][key]==value,key
assert b['colors']==a['colors']
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'look.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_state',styleSection=5);call(op='portrait_state',click='portraitMakeupLookNone');cleared=wait()['recipe']['people'][0]['settings'];assert cleared['parameters']['makeup.lip']==0 and cleared['parameters']['makeup.shadow.left']==0 and cleared['parameters']['skin.smooth']==28 and cleared['colors']==b['colors']
call(op='portrait_state',history=-1);assert wait()['recipe']==edited
call(op='portrait_state',history=-1);assert wait()['recipe']==after
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=edited);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Editable component looks, unrelated disabled effects, placement preservation, one-step undo and saving passed')
