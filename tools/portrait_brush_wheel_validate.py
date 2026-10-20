from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'build/brush-wheel'
OUT.mkdir(exist_ok=True)
photo=ROOT/'docs/test-evidence/M3/quality/john-glenn-source.png'
recipe=json.loads(photo.with_name('john-glenn-recipe.json').read_text(encoding='utf-8'))
call(op='init',settings=str(OUT/'settings'))
try:
    call(op='portrait_window',path=str(photo),recipe=recipe);wait()
    for task,size,amount in [(4,'portraitSmoothBrushSize','portraitSlider_skin.smooth'),(5,'portraitBlemishSize','portraitBlemishAmount'),(6,'portraitWrinkleSize','portraitWrinkleAmount')]:
        call(op='portrait_state',workspaceTask=task)
        if task==4:call(op='portrait_state',skinMode=1)
        wait()
        scale=call(op='portrait_state')['navigation']['scale']
        for modifier,name in [('Ctrl',size),('Shift',amount)]:
            before=call(op='portrait_state',readSlider=name)['sliderValue']
            call(op='portrait_state',brushWheel=60,wheelModifier=modifier)
            assert call(op='portrait_state',readSlider=name)['sliderValue']==before
            call(op='portrait_state',brushWheel=60,wheelModifier=modifier);wait()
            assert call(op='portrait_state',readSlider=name)['sliderValue']==before+5
            call(op='portrait_state',brushWheel=-120,wheelModifier=modifier);wait()
            assert call(op='portrait_state',readSlider=name)['sliderValue']==before
            assert call(op='portrait_state')['navigation']['scale']==scale
    print('Manual skin/blemish/wrinkle: size, strength, partial wheel and unchanged zoom passed')
finally:
    call(op='portrait_close');call(op='shutdown')
