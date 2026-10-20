from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-limbs';call(op='init',settings=str(OUT/'settings'))
recipe=json.loads((OUT/'mediapipe-pose-recipe.json').read_text(encoding='utf-8'));source=OUT/'mediapipe-pose.jpg'
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',select=0,panel=3,linked=False,side=0);wait()
for key in ['body.arm','body.upperArm','body.thigh','body.calf','body.legWidth']:
    before=wait()['recipe'];call(op='portrait_state',parameter=key,value=30);state=wait()
    settings=state['recipe']['people'][0]['settings'];assert settings['limbGeometryVersion']==2
    assert settings['parameters'][key+'.left']==30
    call(op='portrait_state',history=-1);assert wait()['recipe']==before
    call(op='portrait_state',history=1);wait()
    call(op='portrait_state',doubleClickParameter=key);wait()
call(op='portrait_state',parameter='body.waist',value=35);state=wait()
assert state['recipe']['people'][0]['settings']['waistGeometryVersion']==2
call(op='portrait_state',parameter='body.arm',value=40);wait()
call(op='portrait_state',parameter='body.legWidth',value=40);state=wait()
saved=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'limbs.huelith'))
assert saved['roundtrip'] and saved['regionError']<2e-6
recipe=state['recipe'];call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',source=str(source),render=str(OUT/'workflow-reopened.png'))
assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'workflow-reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Limb controls, undo, reset, regions and reopening passed.')
