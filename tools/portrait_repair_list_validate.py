from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-repair-source-drag';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={};x,y,w,h=p['face'];target=[x+w*.5,y+h*.25]
p['settings']['repairs']=[dict(version=1,kind='heal',id=str(i),points=[[target[0],target[1]+i*.02]],offsetX=.025,offsetY=0,radius=.004,feather=.5,opacity=.7) for i in range(2)]
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait();call(op='portrait_state',repairSelect=0);before=wait()['recipe'];call(op='portrait_state',click='portraitRepairManagement');call(op='pump',waitMs=100);call(op='portrait_state',capture=str(OUT/'repair-list.png'),captureWidget='portraitRepairList')
assert wait()['recipe']==before
call(op='portrait_state',repairEnabled=False);disabled=wait()['recipe'];assert not disabled['people'][0]['settings']['repairs'][0]['enabled'];assert disabled['people'][0]['settings']['repairs'][1]==before['people'][0]['settings']['repairs'][1]
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',repairSelect=0);call(op='portrait_state',click='portraitRepairDelete');deleted=wait()['recipe'];assert deleted['people'][0]['settings']['repairs']==before['people'][0]['settings']['repairs'][1:]
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==deleted
r=call(op='portrait_state',source=str(source),render=str(OUT/'list-result.png'),checkRegion=True,save=str(OUT/'list.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='shutdown');print('List selection is read-only; enable/delete independent undo, saved list and ROI passed.')
