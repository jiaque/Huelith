from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'build/makeup-accordion'
OUT.mkdir(exist_ok=True)
photo=ROOT/'docs/test-evidence/M3/quality/john-glenn-source.png'
recipe=json.loads(photo.with_name('john-glenn-recipe.json').read_text(encoding='utf-8'))
call(op='init',settings=str(OUT/'settings'))
try:
    call(op='portrait_window',path=str(photo),recipe=recipe);wait()
    call(op='portrait_state',workspaceTask=2);wait()
    call(op='portrait_state',parameter='makeup.lip',value=45);wait()
    call(op='portrait_state',click='portraitMakeupSection3');wait()
    call(op='portrait_state',parameter='makeup.blush',value=20);wait()
    before=wait()['recipe']
    for section in (0,1,2,3,4,5,6,7,8,9,10,11,0):
        call(op='portrait_state',click='portraitMakeupSection'+str(section))
        assert wait()['recipe']==before
    call(op='portrait_state',capture=str(OUT/'expanded.png'),workspaceLayout=True)
    call(op='portrait_state',click='portraitMakeupSection0')
    assert wait()['recipe']==before
    call(op='portrait_state',capture=str(OUT/'collapsed.png'))
    call(op='portrait_state',source=str(photo),render=str(OUT/'enabled.png'))
    call(op='portrait_state',click='portraitMakeupEnabled0')
    disabled=wait()['recipe']['people'][0]['settings']
    assert disabled['disabledParameters']['makeup.lip']
    assert disabled['parameters']==before['people'][0]['settings']['parameters']
    call(op='portrait_state',source=str(photo),render=str(OUT/'disabled.png'))
    assert np.any(np.array(Image.open(OUT/'enabled.png')) != np.array(Image.open(OUT/'disabled.png')))
    call(op='portrait_state',history=-1)
    assert wait()['recipe']==before
    call(op='portrait_state',click='portraitMakeupSection0');wait()
    call(op='portrait_state',click='portraitMakeupCorrection');wait()
    call(op='portrait_state',capture=str(OUT/'correction.png'))
    print('All makeup categories, collapse preservation, combined parameters and switch undo passed')
finally:
    call(op='portrait_close');call(op='shutdown')
