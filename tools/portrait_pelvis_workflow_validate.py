from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-pelvis';source=ROOT/'research/m2/instances/FudanPed00001.png'
recipe=json.loads((OUT/'FudanPed00001-recipe.json').read_text())
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy'] and r['displayedRevision']==r['previewRevision']:return r
 raise AssertionError(r['status'])
call(op='init',settings=str(OUT/'settings'));call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',panel=3,select=1,linked=False,side=0);wait()
call(op='portrait_state',parameter='body.hipWidth',value=60);state=wait()
assert state['recipe']['people'][1]['settings']['parameters']['body.hipWidth.left']==60
before=state['recipe'];call(op='portrait_state',doubleClickParameter='body.hipWidth');assert wait()['recipe']['people'][1]['settings']['parameters']['body.hipWidth.left']==0
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',linked=True);before=wait()['recipe']
call(op='portrait_state',parameter='body.hipWidth',value=40);assert wait()['recipe']==before
call(op='portrait_state',panel=0,parameter='face.width',value=-10);wait()
before=wait()['recipe'];call(op='portrait_state',liquify=True);assert wait()['recipe']==before
# A smaller ordinary drag must still work after the rejected oversized stroke.
call(op='portrait_state',brushSize=25)
face=before['people'][1]['face'];x,y,fw,fh=face
call(op='portrait_state',paintPath=[[x+fw*.5,y+fh*.6],[x+fw*.54,y+fh*.6]])
assert wait()['recipe']!=before
call(op='portrait_state',panel=3,linked=False,side=0,parameter='body.hipWidth',value=-45);saved=wait()['recipe']
assert saved['people'][1]['settings']['parameters']['body.hipWidth.left']==-45
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'workflow.huelith'),checkRegion=True)
assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=saved);wait()
call(op='portrait_state',source=str(source),render=str(OUT/'workflow-reopened.png'))
assert np.array_equal(np.array(Image.open(OUT/'workflow.png')),np.array(Image.open(OUT/'workflow-reopened.png')))
call(op='portrait_close')
source=ROOT/'research/m3/portraits/jassi-standing.jpg'
recipe=json.loads((OUT/'jassi-standing-recipe.json').read_text())
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=3,select=0,linked=False);wait()
for side,amount in [(0,65),(1,-40),(0,25),(1,50)]:
 call(op='portrait_state',side=side);before=wait()['recipe']['people'][0]['settings'].get('parameters') or {}
 other='body.hipWidth.'+('right' if side==0 else 'left')
 call(op='portrait_state',parameter='body.hipWidth',value=amount)
 params=wait()['recipe']['people'][0]['settings']['parameters']
 assert params[other]==before[other] if other in before else other not in params
 assert params['body.hipWidth.'+('left' if side==0 else 'right')]==amount
call(op='portrait_state',doubleClickParameter='body.hipWidth');params=wait()['recipe']['people'][0]['settings']['parameters']
assert params['body.hipWidth.right']==0 and params['body.hipWidth.left']==25
call(op='portrait_state',history=-1);assert wait()['recipe']['people'][0]['settings']['parameters']['body.hipWidth.right']==50
call(op='portrait_state',linked=True,parameter='body.hipWidth',value=-30);saved=wait()['recipe']
assert saved['people'][0]['settings']['parameters']['body.hipWidth.left']==saved['people'][0]['settings']['parameters']['body.hipWidth.right']==-30
r=call(op='portrait_state',source=str(source),render=str(OUT/'jassi-standing-workflow.png'),save=str(OUT/'bilateral.huelith'),checkRegion=True)
assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=saved);wait()
call(op='portrait_state',source=str(source),render=str(OUT/'jassi-standing-reopened.png'))
assert np.array_equal(np.array(Image.open(OUT/'jassi-standing-workflow.png')),np.array(Image.open(OUT/'jassi-standing-reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Hip bilateral independence/linkage, reset/history, invalid-side rollback, face/liquify combination and save/reopen passed.')
