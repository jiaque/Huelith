from pathlib import Path
import base64,io
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/body-combination';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);s=call(op='portrait_state')
  if s['ready'] and not s['previewBusy'] and s['displayedRevision']==s['previewRevision']:return s
 raise AssertionError(s['status'])
source=ROOT/'research/m3/portraits/jassi-standing.jpg'
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text())
recipe['people'][0]['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=2);wait()
call(op='portrait_state',parameter='head.size',value=-12);wait()
call(op='portrait_state',panel=3);before=wait()['recipe']
call(op='portrait_state',click='portraitCombination');first=wait()['recipe']
params=first['people'][0]['settings']['parameters'];assert params['head.size']==-12
assert params.get('body.legWidth.left',0)==10 and params.get('body.legWidth.right',0)==10
# Hands on the waist occlude that contour: keep the visible legs only.
assert params.get('body.waist',0)==0,params
call(op='portrait_state',click='portraitCombination');assert wait()['recipe']==first
call(op='portrait_state',click='portraitPresetDiscard');assert wait()['recipe']==before
call(op='portrait_state',click='portraitCombination');wait();call(op='portrait_state',click='portraitPresetApply');wait()
call(op='portrait_state',linked=False,side=0,parameter='body.legWidth',value=25)
assert wait()['recipe']['people'][0]['settings']['parameters']['body.legWidth.left']==25
call(op='portrait_close')
# Fresh entry must prepare the head automatically, and the card shares the
# body-page operation instead of applying a different legacy preset.
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=3);before=wait()['recipe']
call(op='portrait_state',click='portraitCombination');button=wait()['recipe']
assert button['people'][0]['settings']['parameters']['head.size']==-8
call(op='portrait_state',click='portraitPresetDiscard');wait();call(op='portrait_state',panel=15);wait()
call(op='portrait_state',presetCard=1);card=wait()['recipe'];assert card==button
call(op='portrait_state',presetStrength=50);half=wait()['recipe']['people'][0]['settings']['parameters']
assert half['head.size']==-4 and half['body.legWidth.left']==5
call(op='portrait_state',presetStrength=0);zero=wait()['recipe']['people'][0]['settings']['parameters'];assert all(v==0 for v in zero.values())
call(op='portrait_state',presetStrength=100);assert wait()['recipe']==button
call(op='portrait_state',click='portraitPresetApply');saved=wait()['recipe']
r=call(op='portrait_state',source=str(source),render=str(OUT/'jassi-standing-combination.png'),save=str(OUT/'combination.huelith'),checkRegion=True)
assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=saved);wait();call(op='portrait_state',source=str(source),render=str(OUT/'jassi-standing-reopened.png'))
assert np.array_equal(np.array(Image.open(OUT/'jassi-standing-combination.png')),np.array(Image.open(OUT/'jassi-standing-reopened.png')))
call(op='portrait_close')
source=ROOT/'research/m2/instances/FudanPed00001.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/FudanPed00001-recipe.json').read_text())
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=1,panel=3);before=wait()['recipe']
call(op='portrait_state',source=str(source),render=str(OUT/'multiple-before.png'))
call(op='portrait_state',click='portraitCombination');after=wait()['recipe'];assert after['people'][0]==before['people'][0]
assert after['people'][1]!=before['people'][1]
call(op='portrait_state',source=str(source),render=str(OUT/'multiple-after.png'))
a=np.array(Image.open(OUT/'multiple-before.png'));b=np.array(Image.open(OUT/'multiple-after.png'))
mask=Image.open(io.BytesIO(base64.b64decode(before['people'][0]['bodyMask']))).resize((a.shape[1],a.shape[0]),Image.Resampling.NEAREST)
assert np.array_equal(a[np.array(mask)>200],b[np.array(mask)>200])
call(op='portrait_close');call(op='shutdown');print('Shared card/button, visibility filtering, existing edits, strengths, cancel/apply, current-person isolation and save/reopen passed.')
