"""D12 combined whole-leg/legLength edits through actual offscreen controls."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-height'
BASE=ROOT/'docs/test-evidence/M3/semantic-back'
source=BASE/'mr-yoga-side-bend.jpg'
base=json.loads((BASE/'mr-yoga-recipe.json').read_text())
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
call(op='init',settings=str(OUT/'legLength-workflow-settings'))
call(op='portrait_window',path=str(source),recipe=base);wait()


call(op='portrait_state',select=0,panel=15);before=wait()
call(op='portrait_state',panel=3);wait()
call(op='portrait_state',incomingPreset={'heightVersion':2,'parameters':{'body.height':60}})
call(op='portrait_state',panel=15);before=wait();call(op='portrait_state',presetCard=5);r=wait()
assert r['people'][0]['settings']['parameters']['body.height']==60
call(op='portrait_state',panel=3);committed=wait()
call(op='portrait_state',history=-1);assert wait()==before
call(op='portrait_state',history=1);assert wait()==committed
call(op='portrait_state',panel=0);call(op='portrait_state',parameter='face.width',value=10);r=wait()
assert r['people'][0]['settings']['parameters']['body.height']==60
assert r['people'][0]['settings']['parameters']['face.width']==10
call(op='portrait_close');call(op='shutdown')
print('Height preset card, undo/redo and continued face adjustment passed.')
