"""D14 combined shoulder-line/width edits through actual offscreen controls."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-shoulder'
BASE=ROOT/'docs/test-evidence/M3/quality'
source=BASE/'jessica-watkins-source.png'
base=json.loads((OUT/'jessica-watkins-production-recipe.json').read_text())
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
call(op='init',settings=str(OUT/'pose-failure-settings'));call(op='portrait_window',path=str(source),recipe=base);wait()
call(op='portrait_state',select=0,panel=3);before=wait()
call(op='portrait_state',parameter='body.shoulderWidth',value=50);after=wait()
assert after['people'][0]['settings']['parameters']==before['people'][0]['settings']['parameters']
call(op='portrait_state',panel=0);wait();call(op='portrait_state',parameter='face.width',value=15);after=wait()
assert after['people'][0]['settings']['parameters']['face.width']==15
call(op='portrait_state',source=str(source),render=str(OUT/'pose-failure-face.png'));call(op='portrait_close');call(op='shutdown')
print('Unavailable pose rejects body change; face edit remains functional.')
