"""D08 combined whole-leg/thigh edits through actual offscreen controls."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-legs'
BASE=ROOT/'docs/test-evidence/M3/quality'
source=BASE/'FudanPed00001-source.png'
base=json.loads((BASE/'FudanPed00001-recipe.json').read_text())
for p in base['people']:p['settings']['parameters']={}
def wait():
    end=time.monotonic()+45
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:
            assert r['displayedRevision']==r['previewRevision'],r['status']
            return r['recipe']
    raise AssertionError(r['status'])
def render(name):
    call(op='portrait_state',source=str(source),render=str(OUT/(name+'.png')))
    return np.asarray(Image.open(OUT/(name+'.png')))
call(op='init',settings=str(OUT/'thigh-workflow-settings'))
call(op='portrait_window',path=str(source),recipe=base);wait()
for person in range(len(base['people'])):
    call(op='portrait_state',select=person,panel=3,linked=True);wait()
    call(op='portrait_state',parameter='body.legWidth',value=35);leg=wait()
    leg_image=render('thigh-leg-only')
    call(op='portrait_state',parameter='body.thigh',value=50);combined=wait()
    params=combined['people'][person]['settings']['parameters']
    assert params['body.thigh.left']==params['body.thigh.right']==50
    assert params['body.legWidth.left']==params['body.legWidth.right']==35
    assert not np.array_equal(leg_image,render('thigh-combined'))
    call(op='portrait_state',history=-1);assert wait()==leg
    call(op='portrait_state',history=1);assert wait()==combined
    call(op='portrait_state',doubleClickParameter='body.thigh');reset=wait()
    params=reset['people'][person]['settings']['parameters']
    assert params.get('body.thigh.left',0)==params.get('body.thigh.right',0)==0
    assert params['body.legWidth.left']==params['body.legWidth.right']==35
    assert np.array_equal(leg_image,render('thigh-reset'))
    call(op='portrait_state',history=-1);assert wait()==combined
saved=call(op='portrait_state',source=str(source),render=str(OUT/'thigh-workflow.png'),save=str(OUT/'thigh.huelith'),checkRegion=True)
assert saved['roundtrip'] and saved['regionError']<2e-6
final=wait();call(op='portrait_close')
call(op='portrait_window',path=str(source),recipe=final);wait()
assert np.array_equal(np.asarray(Image.open(OUT/'thigh-workflow.png')),render('thigh-reopened'))
call(op='portrait_close');call(op='shutdown')
(OUT/'thigh-workflow-results.json').write_text(json.dumps(dict(combination=True,undoRedo=True,independentReset=True,roundtrip=saved['roundtrip'],regionError=saved['regionError'])))
print('Thigh combination, independent reset, history, save and reopened rendering passed.')
