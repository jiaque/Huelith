from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-repair-preserve';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={};x,y,w,h=p['face'];target=[x+w*.5,y+h*.25]
p['settings']['repairs']=[dict(version=1,kind='heal',id=str(i),points=[[target[0],target[1]+i*.02]],offsetX=.025,offsetY=0,radius=.004,feather=.5,opacity=.7) for i in range(2)]
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
p['settings']['parameters']={}
p['settings']['repairs']=p['settings']['repairs'][:1]
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait();before=wait()['recipe']
call(op='portrait_state',source=str(source),render=str(OUT/'unprotected.png'))
call(op='portrait_state',click='portraitPreserveBrush',brushSize=40)
call(op='portrait_state',paintPath=[target,[target[0]+.001,target[1]]]);protected=wait()['recipe']
assert 'repair.preserve' in protected['people'][0]['settings']['masks']
call(op='portrait_state',source=str(source),render=str(OUT/'protected.png'))
a=np.array(Image.open(OUT/'unprotected.png')).astype(float);b=np.array(Image.open(OUT/'protected.png')).astype(float)
assert np.max(abs(a-b))>1,'protection must affect existing repair'
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==protected
call(op='portrait_state',click='portraitPreserveErase',brushSize=60);call(op='portrait_state',paintPath=[target,[target[0]+.001,target[1]]]);wait()
r=call(op='portrait_state',source=str(source),render=str(OUT/'erased.png'),checkRegion=True,save=str(OUT/'preserve.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
c=np.array(Image.open(OUT/'erased.png')).astype(float);assert np.mean(abs(a-c))<np.mean(abs(a-b))
call(op='portrait_close')
p['settings']['blemishes']=[dict(x=target[0],y=target[1],radius=.01,state='pending')]
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait();call(op='portrait_state',blemishSelect=0);call(op='portrait_state',click='portraitPreserveBlemish');marked=wait()['recipe'];assert marked['people'][0]['settings']['blemishes'][0]['state']=='preserved'
call(op='portrait_state',blemishSelect=0);call(op='portrait_state',click='portraitApplyBlemish');assert wait()['recipe']==marked
call(op='portrait_state',source=str(source),render=str(OUT/'marked.png'))
d=np.array(Image.open(OUT/'marked.png')).astype(float);assert np.max(abs(a-d))>1
call(op='portrait_state',blemishSelect=0);call(op='portrait_state',click='portraitPreserveBlemish');assert wait()['recipe']['people'][0]['settings']['blemishes'][0]['state']=='pending'
call(op='portrait_state',history=-1);assert wait()['recipe']==marked
call(op='portrait_state',source=str(source),render=str(OUT/'saved-mark.png'),checkRegion=True,save=str(OUT/'marked.huelith'))
call(op='portrait_close');call(op='shutdown');print('Preserve brush blocks existing repair, erases, undo/redo passed')
