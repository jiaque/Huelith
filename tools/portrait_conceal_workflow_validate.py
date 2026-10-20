from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-conceal';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait();call(op='portrait_state',click='portraitConcealBrush',brushSize=8)
x,y,w,h=recipe['people'][0]['face'];before=wait()['recipe'];call(op='portrait_state',paint=[x+w*.5,y+h*.25]);r=wait();assert len(r['recipe']['people'][0]['settings'].get('concealColor',[]))==3,r['status'];assert 'repair.conceal' in r['recipe']['people'][0]['settings']['masks'];assert not r['recipe']['people'][0]['settings'].get('repairs',[])
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);wait();saved=call(op='portrait_state',source=str(source),render=str(OUT/'repair.png'),checkRegion=True,save=str(OUT/'repair.huelith'));assert saved['roundtrip'] and saved['regionError']<2e-6
assert np.max(abs(np.asarray(Image.open(source).convert('RGB'),dtype=int)-np.asarray(Image.open(OUT/'repair.png').convert('RGB'),dtype=int)))>0
call(op='portrait_close');call(op='shutdown');print('Direct conceal painting, automatic reference, separate mask, undo and save passed.')


