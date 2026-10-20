from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'build/orientation-v2'
(OUT/'tmp').mkdir(exist_ok=True)
os.environ['TEMP']=os.environ['TMP']=str(OUT/'tmp')
os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe')
photo=ROOT/'docs/test-evidence/M3/quality/chris-hadfield-source.png'
recipe=json.loads((ROOT/'docs/test-evidence/M3/workspace-b01/orientation/chris-hadfield-recipe.json').read_text(encoding='utf-8'))
recipe['sourceHash']=json.loads(photo.with_name('chris-hadfield-recipe.json').read_text(encoding='utf-8'))['sourceHash']
recipe['people'][0]['settings']['parameters']={'head.yaw':30}
recipe['people'][0]['settings'].pop('headOrientationVersion',None)
call(op='init',settings=str(OUT/'workflow-settings'))
try:
    call(op='portrait_window',path=str(photo),recipe=recipe,models=str(ROOT/'build/windows-release/models'));before=wait()['recipe']
    assert before['people'][0]['settings'].get('headOrientationVersion',1)==1
    call(op='portrait_state',workspaceTask=3,panel=2,select=0);wait()
    call(op='portrait_state',parameter='head.yaw',value=70);state=wait()
    assert state['recipe']['people'][0]['settings'].get('headOrientationVersion')==2,state['status']
    call(op='portrait_state',history=-1);assert wait()['recipe']==before
    call(op='portrait_state',history=1);wait()
    call(op='portrait_state',parameter='head.pitch',value=-60);wait()
    call(op='portrait_state',panel=0);wait()
    call(op='portrait_state',parameter='face.width',value=25);wait()
    call(op='portrait_state',panel=2);wait()
    call(op='portrait_state',parameter='head.yaw',value=-70);state=wait()
    assert state['recipe']['people'][0]['settings']['parameters']['face.width']==25
    result=call(op='portrait_state',source=str(photo),render=str(OUT/'combined.png'),save=str(OUT/'combined.huelith'),checkRegion=True)
    assert result['roundtrip'] and result['regionError']<2e-6
    saved=wait()['recipe'];call(op='portrait_close')
    call(op='portrait_window',project=str(OUT/'combined.huelith'));assert wait()['recipe']==saved
    call(op='portrait_state',source=str(photo),render=str(OUT/'reopened.png'))
    assert np.array_equal(np.array(Image.open(OUT/'combined.png')),np.array(Image.open(OUT/'reopened.png')))
    print('Legacy preservation, migration undo/redo, combined face/head, save/reopen and ROI passed')
finally:call(op='portrait_close');call(op='shutdown')
