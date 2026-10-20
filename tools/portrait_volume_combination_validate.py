"""Actual asynchronous one-click UI, stale/cancel gates and displayed preview."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,base64,io
OUT=ROOT/'docs/test-evidence/M3/semantic-volume-combination';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png'
real=json.loads((ROOT/'docs/test-evidence/M3/semantic-forehead-volume/real-recipe.json').read_text(encoding='utf-8'));real['people'][0]['settings']['parameters']={'feature.noseWidth':12}
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=real);wait();call(op='portrait_state',panel=0,select=0);before=wait();assert 'face.foreheadFill' not in before['recipe']['people'][0]['settings']['parameters']
start=time.monotonic();call(op='portrait_state',click='portraitVolumeCombination');dispatch=time.monotonic()-start;assert dispatch<.5,dispatch
applied=wait();s=applied['recipe']['people'][0]['settings'];params=s['parameters'];assert params['face.foreheadFill']==40 and params['face.malarShape.left']==20 and params['face.volume']==20 and params['feature.noseWidth']==12
assert applied['history']==before['history']+1
call(op='portrait_state',history=-1);assert wait()['recipe']==before['recipe'];call(op='portrait_state',history=1);assert wait()['recipe']==applied['recipe']
# Existing group reset really restores only that region, including both sides.
call(op='portrait_state',click='portraitGroupReset_0_苹果肌');reset=wait()['recipe']['people'][0]['settings']['parameters'];assert 'face.malarShape.left' not in reset and 'face.malarTone.left' not in reset and reset['face.tearFill.left']==params['face.tearFill.left'];call(op='portrait_state',history=-1);wait()
r=call(op='portrait_state',source=str(source),render=str(OUT/'combined.png'),save=str(OUT/'combination.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
# Stale work must not overwrite a later user edit; cancel does not apply a
# partial analysis. Both run against the production asynchronous callback.
call(op='portrait_state',click='portraitVolumeCombination');call(op='portrait_state',parameter='face.width',value=7);stale=call(op='portrait_state')['recipe'];assert wait()['recipe']==stale
call(op='portrait_state',click='portraitVolumeCombination');call(op='portrait_state',click='portraitCancelCompute');assert wait()['recipe']==stale
call(op='portrait_close')
# Missing independent skin ranges are explicitly skipped, not substituted by
# whole-face guesses, and do not clear existing editing settings.
missing=copy.deepcopy(real);missing['people'][0]['settings']['automaticMasks'].pop('faceSkin',None)
call(op='portrait_window',path=str(source),recipe=missing);wait();call(op='portrait_state',panel=0,select=0);unknown=wait()['recipe'];call(op='portrait_state',click='portraitVolumeCombination');assert wait()['recipe']==unknown;call(op='portrait_close')
# Low-confidence geometry on one side must not be filled by mirroring the
# other, and the bilateral outline must remain zero when one side is unknown.
partial=copy.deepcopy(real)
for index in [1,36,39,41]:partial['people'][0]['landmarks'][index][2]=.05
call(op='portrait_window',path=str(source),recipe=partial);wait();call(op='portrait_state',panel=0,select=0,click='portraitVolumeCombination');partial_state=wait();pp=partial_state['recipe']['people'][0]['settings']['parameters'];assert pp['face.malarShape.left']==20 and pp['face.malarShape.right']==0 and pp['face.tearFill.right']==0 and pp['face.cheekTone.right']==0 and pp['face.volume']==0;assert '跳过' in partial_state['status'];call(op='portrait_close')
# Exercise the displayed QGraphicsView, not just the separate export renderer.
clean=copy.deepcopy(real);clean['people'][0]['settings']['parameters']={};call(op='portrait_window',path=str(source),recipe=clean);wait();call(op='portrait_state',panel=0,select=0,linked=False,side=0);wait()
for key in ['face.foreheadFill','face.tearFill','face.malarTone','face.cheekTone','face.orbitalTone','face.perioralTone']:
 call(op='portrait_state',capture=str(OUT/'preview-before.png'),captureWidget='portraitView');call(op='portrait_state',parameter=key,value=100);wait();call(op='portrait_state',capture=str(OUT/(key+'-preview.png')),captureWidget='portraitView')
 a=np.array(Image.open(OUT/'preview-before.png')).astype(int);b=np.array(Image.open(OUT/(key+'-preview.png'))).astype(int);assert a.shape==b.shape and abs(a-b).max()>0,('displayed preview did not change',key)
 call(op='portrait_state',parameter=key,value=0);wait()
call(op='portrait_close');call(op='shutdown');print('One-click async dispatch, single undo, regional reset, stale/cancel, missing-skin skip, save/ROI and actual displayed tonal previews passed')
