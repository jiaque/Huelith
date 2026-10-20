"""D13 independent neck width/length edits through actual offscreen controls."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-neck'
BASE=ROOT/'docs/test-evidence/M3/quality'
source=BASE/'leroy-chiao-source.png'
base=json.loads((ROOT/'docs/test-evidence/M3/semantic-shoulder/leroy-chiao-production-recipe.json').read_text())
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
call(op='init',settings=str(OUT/'swan-workflow-settings'))
call(op='portrait_window',path=str(source),recipe=base);wait()

call(op='portrait_state',select=0,panel=3);before=wait()
call(op='portrait_state',click='portraitSwanNeck');combined=wait()
p=combined['people'][0]['settings']['parameters'];assert p['body.neck']==-30 and p['body.neckLength']==30
call(op='portrait_state',click='portraitSwanNeck');assert wait()==combined,'repeated combination accumulates'
call(op='portrait_state',doubleClickParameter='body.neckLength');reset=wait()
p=reset['people'][0]['settings']['parameters'];assert p['body.neck']==-30 and p.get('body.neckLength',0)==0
call(op='portrait_state',history=-1);assert wait()==combined
call(op='portrait_state',doubleClickParameter='body.neck');reset=wait()
p=reset['people'][0]['settings']['parameters'];assert p.get('body.neck',0)==0 and p['body.neckLength']==30
call(op='portrait_state',history=-1);assert wait()==combined
saved=call(op='portrait_state',source=str(source),render=str(OUT/'swan-workflow.png'),save=str(OUT/'swan.huelith'),checkRegion=True)
assert saved['roundtrip'] and saved['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=combined);wait()
assert np.array_equal(np.asarray(Image.open(OUT/'swan-workflow.png')),render('swan-reopened'))
call(op='portrait_close');call(op='shutdown');print('Swan-neck repeat, separate reset, undo, ROI and save/reopen passed.')
