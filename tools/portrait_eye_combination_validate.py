from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-eye-combination';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+45
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])


recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());s=recipe['people'][0]['settings'];s['parameters']={'makeup.lip':25,'makeup.shadow.right':13,'makeup.lash.right':18};s['linked']=False;s['side']=0;s['colors']={'makeup.shadow':[.2,.08,.05]};s['lashMaterial']='long';s['eyeLashVersion']=2;s['eyeShadowVersion']=2
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=2);before=wait()['recipe'];call(op='portrait_state',click='portraitEyeCombinationDefined');after=wait()['recipe'];settings=after['people'][0]['settings'];params=settings['parameters'];assert params['makeup.shadow.left']==50 and params['makeup.lash.left']==55
assert params['makeup.shadow.right']==13 and params['makeup.lash.right']==18 and params['makeup.lip']==25
assert settings['colors']==before['people'][0]['settings']['colors'] and settings['lashMaterial']=='long'
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
call(op='portrait_state',parameter='makeup.lash',value=20);edited=wait()['recipe'];assert edited['people'][0]['settings']['parameters']['makeup.shadow.left']==50
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'combination.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=edited);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Eye combination single-side targeting, one-step undo, independent adjustment and saving passed')
