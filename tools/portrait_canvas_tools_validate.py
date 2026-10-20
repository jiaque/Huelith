from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'build/portrait-canvas-tools'
OUT.mkdir(exist_ok=True)
photo=ROOT/'docs/test-evidence/M3/quality/john-glenn-source.png'
recipe=json.loads(photo.with_name('john-glenn-recipe.json').read_text(encoding='utf-8'))
call(op='init',settings=str(OUT/'settings'))
try:
    call(op='portrait_window',path=str(photo),recipe=recipe)
    wait()
    for task in (5,6):
        call(op='portrait_state',workspaceTask=task)
        state=wait()
        assert state['navigation']['cursor']==2,state['navigation'] # CrossCursor, without touching size.
        before=state['recipe']
        call(op='portrait_state',canvasKey='H')
        assert wait()['navigation']['cursor']==17
        call(op='portrait_state',canvasKey='B')
        assert wait()['navigation']['cursor']==2
        call(op='portrait_state',canvasKey='Space')
        assert wait()['navigation']['temporary']
        call(op='portrait_state',canvasKey='Space',keyRelease=True)
        assert wait()['navigation']['cursor']==2
        call(op='portrait_state',click='portraitCanvasHand')
        assert wait()['navigation']['cursor']==17
        call(op='portrait_state',click='portraitCanvasBrush')
        assert wait()['recipe']==before
        assert wait()['navigation']['cursor']==2
    call(op='portrait_state',workspaceTask=0)
    wait()
    call(op='portrait_state',capture=str(OUT/'volume.png'))
    # Existing driver uses the production tab control.
    call(op='portrait_state',volumeMode=1)
    wait()
    call(op='portrait_state',capture=str(OUT/'lift.png'))
    print('Entry cursor, B/H, temporary space, buttons and recipe preservation passed')
finally:
    call(op='portrait_close')
    call(op='shutdown')
