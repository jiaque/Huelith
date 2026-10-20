"""D11 combined whole-leg/legLength edits through actual offscreen controls."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-leg-length'
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
call(op='init',settings=str(OUT/'legLength-workflow-settings'))
call(op='portrait_window',path=str(source),recipe=base);wait()

call(op='portrait_state',select=0,panel=3,linked=True);wait()
call(op='portrait_state',parameter='body.legLength',value=50);wait()
call(op='portrait_state',linked=False,side=0);before=wait()
call(op='portrait_state',parameter='body.legLength',value=75);after=wait()
assert after==before,'rejected unequal edit left partial settings'
call(op='portrait_state',parameter='body.legLength',value=50);assert wait()==before
call(op='portrait_state',panel=0);call(op='portrait_state',parameter='face.width',value=10);r=wait()
assert r['people'][0]['settings']['parameters']['face.width']==10
call(op='portrait_state',incomingPreset={'legLengthVersion':2,'parameters':{'body.legLength.left':40,'body.legLength.right':40}})
call(op='portrait_state',panel=15);wait();call(op='portrait_state',presetCard=5)
r=wait()
p=r['people'][0]['settings']['parameters'];assert p['body.legLength.left']==p['body.legLength.right']==40,p
call(op='portrait_state',panel=3);committed=wait()
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=committed);wait()
call(op='portrait_state',select=0)
call(op='portrait_state',incomingPreset={'legLengthVersion':2,'parameters':{'body.legLength.left':70,'body.legLength.right':20}})
call(op='portrait_state',panel=15);before=wait();call(op='portrait_state',presetCard=5)
after=wait()
if after!=before:
 print('before',before['people'][0]['settings']['parameters'],flush=True)
 print('after',after['people'][0]['settings']['parameters'],flush=True)
 print(call(op='portrait_state')['status'],flush=True)
 call(op='portrait_close');call(op='shutdown')
 raise AssertionError('rejected preset changed settings')
call(op='portrait_close');call(op='shutdown')
print('Equal preset, rejected unequal preset, side switching and subsequent face edits passed.')
