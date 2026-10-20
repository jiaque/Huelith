from pathlib import Path
import cv2, copy
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-makeup-repair';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={};p['settings']['mouthTopologyVersion']=2
w,h=Image.open(source).size
records=json.loads((OUT/'cases.json').read_text())
case=records[1];target=case['target'];print(case['part'],flush=True)
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
for case in records:
    target=case['target'];print(case['part'],flush=True)
    call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait();call(op='portrait_state',click='portraitMakeupRepair');before=wait()['recipe']
    call(op='portrait_state',paint=target);state=wait();strokes=state['recipe']['people'][0]['settings'].get('repairs',[]);assert len(strokes)==1,(state['status'],strokes);assert strokes[0]['makeupPart']==case['part']
    call(op='portrait_state',history=-1);assert wait()['recipe']==before
    call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'makeup.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
    # Moving a selected source to the forehead must be rejected without changing the recipe.
    call(op='portrait_state',repairSelect=0);valid=wait()['recipe'];call(op='portrait_state',paint=[.5,.2],alt=True);assert wait()['recipe']==valid
    call(op='portrait_close')
call(op='shutdown');print('Direct default tool works in all six parts with undo, source rejection and save.')
