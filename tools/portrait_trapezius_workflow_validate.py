"""D17 shoulder curve/elevation edits with current production pose detection."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-shoulder'
BASE=ROOT/'docs/test-evidence/M3/quality'
source=BASE/'leroy-chiao-source.png'
base=json.loads((OUT/'leroy-chiao-production-recipe.json').read_text())
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
call(op='init',settings=str(OUT/'trapezius-workflow-settings'))
call(op='portrait_window',path=str(source),recipe=base);wait()
for person in range(len(base['people'])):
    call(op='portrait_state',select=person,panel=3,linked=True);wait()
    call(op='portrait_state',parameter='body.shoulder',value=35);leg=wait()
    leg_image=render('trapezius-leg-only')
    call(op='portrait_state',parameter='body.trapezius',value=50);combined=wait()
    assert combined['people'][person]['settings']['trapeziusGeometryVersion']==2
    params=combined['people'][person]['settings']['parameters']
    assert params['body.trapezius.left']==params['body.trapezius.right']==50
    assert params['body.shoulder.left']==params['body.shoulder.right']==35
    assert not np.array_equal(leg_image,render('trapezius-combined'))
    call(op='portrait_state',history=-1);assert wait()==leg
    call(op='portrait_state',history=1);assert wait()==combined
    call(op='portrait_state',doubleClickParameter='body.trapezius');reset=wait()
    params=reset['people'][person]['settings']['parameters']
    assert params.get('body.trapezius.left',0)==params.get('body.trapezius.right',0)==0
    assert params['body.shoulder.left']==params['body.shoulder.right']==35
    assert np.array_equal(leg_image,render('trapezius-reset'))
    call(op='portrait_state',history=-1);assert wait()==combined
saved=call(op='portrait_state',source=str(source),render=str(OUT/'trapezius-workflow.png'),save=str(OUT/'trapezius.huelith'),checkRegion=True)
assert saved['roundtrip'] and saved['regionError']<2e-6
final=wait();call(op='portrait_close')
call(op='portrait_window',path=str(source),recipe=final);wait()
assert np.array_equal(np.asarray(Image.open(OUT/'trapezius-workflow.png')),render('trapezius-reopened'))
call(op='portrait_close');call(op='shutdown')
(OUT/'trapezius-workflow-results.json').write_text(json.dumps(dict(combination=True,undoRedo=True,independentReset=True,roundtrip=saved['roundtrip'],regionError=saved['regionError'])))
print('Shoulder contour combination, independent reset, history, save and reopened rendering passed.')
