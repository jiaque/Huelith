"""D14 combined shoulder-line/width edits through actual offscreen controls."""
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
call(op='init',settings=str(OUT/'shoulder-elevation-workflow-settings'))
call(op='portrait_window',path=str(source),recipe=base);wait()
for person in range(len(base['people'])):
    call(op='portrait_state',select=person,panel=3,linked=True);wait()
    call(op='portrait_state',parameter='body.shoulderWidth',value=35);leg=wait()
    leg_image=render('shoulder-elevation-leg-only')
    call(op='portrait_state',parameter='body.shoulder',value=50);combined=wait()
    params=combined['people'][person]['settings']['parameters']
    assert params['body.shoulder.left']==params['body.shoulder.right']==50
    assert params['body.shoulderWidth.left']==params['body.shoulderWidth.right']==35
    assert not np.array_equal(leg_image,render('shoulder-elevation-combined'))
    call(op='portrait_state',history=-1);assert wait()==leg
    call(op='portrait_state',history=1);assert wait()==combined
    call(op='portrait_state',doubleClickParameter='body.shoulder');reset=wait()
    params=reset['people'][person]['settings']['parameters']
    assert params.get('body.shoulder.left',0)==params.get('body.shoulder.right',0)==0
    assert params['body.shoulderWidth.left']==params['body.shoulderWidth.right']==35
    assert np.array_equal(leg_image,render('shoulder-elevation-reset'))
    call(op='portrait_state',history=-1);assert wait()==combined
call(op='portrait_state',panel=3,linked=False,side=0);wait()
call(op='portrait_state',parameter='body.shoulder',value=-25);one=wait()
params=one['people'][0]['settings']['parameters'];assert params['body.shoulder.left']==-25 and params['body.shoulder.right']==50
call(op='portrait_state',side=1);wait();call(op='portrait_state',parameter='body.shoulder',value=15);two=wait()
params=two['people'][0]['settings']['parameters'];assert params['body.shoulder.left']==-25 and params['body.shoulder.right']==15
settings=two['people'][0]['settings'];assert settings['shoulderGeometryVersion']==2
portable=call(op='portrait_geometry',portableSkin=settings,bodyPreset=True,targetSettings={})
assert portable['portable']['shoulderGeometryVersion']==portable['merged']['shoulderGeometryVersion']==2
call(op='portrait_state',panel=0);wait();call(op='portrait_state',parameter='face.width',value=15);face=wait()
assert face['people'][0]['settings']['parameters']['body.shoulder.left']==-25
saved=call(op='portrait_state',source=str(source),render=str(OUT/'shoulder-elevation-workflow.png'),save=str(OUT/'shoulder-elevation.huelith'),checkRegion=True)
assert saved['roundtrip'] and saved['regionError']<2e-6
final=wait();call(op='portrait_close')
call(op='portrait_window',path=str(source),recipe=final);wait()
assert np.array_equal(np.asarray(Image.open(OUT/'shoulder-elevation-workflow.png')),render('shoulder-elevation-reopened'))
call(op='portrait_close');call(op='shutdown')
(OUT/'shoulder-elevation-workflow-results.json').write_text(json.dumps(dict(combination=True,undoRedo=True,independentReset=True,roundtrip=saved['roundtrip'],regionError=saved['regionError'])))
print('Shoulder width combination, independent reset, history, save and reopened rendering passed.')
