"""Actual viewport gestures: interruption must commit once and allow the next stroke."""
from pathlib import Path
exec(compile(Path('tools/portrait_brush_target_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0], 'tools/portrait_brush_target_validate.py', 'exec'))
OUT=ROOT/'docs/test-evidence/M3/brush-capture';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
records=[]
for panel,tab in [(4,None),(7,None),(12,2)]:
    baseline=None
    for ending in ['release','outside','space','focus','ungrab','deactivate']:
        call(op='portrait_window',path=str(source),recipe=base);wait()
        call(op='portrait_state',select=0,panel=panel)
        if tab is not None:call(op='portrait_state',styleTab=tab)
        wait()
        if panel!=4:call(op='portrait_state',paint=point(.5,-.05) if tab else point(.25,.5),alt=True)
        before=wait();path=[point(.4,.55),point(.46,.55),point(.52,.55)]
        call(op='portrait_state',paintPath=path,paintEnd=ending if ending in ['ungrab','deactivate'] else 'hold')
        if ending=='space':
            call(op='portrait_state',viewSpace=True)
            call(op='portrait_state',paintRelease=path[-1])
            call(op='portrait_state',viewSpace=False)
        elif ending=='focus':call(op='portrait_state',viewFocusOut=True)
        elif ending in ['outside','release']:call(op='portrait_state',paintRelease=[-1,-1] if ending=='outside' else path[-1])
        after=wait()
        assert after['history']==before['history']+1,(panel,ending,'history')
        recipe=after['recipe']
        comparable=copy.deepcopy(recipe)
        for person in comparable['people']:
            for stroke in person['settings'].get('repairs',[]):stroke.pop('id',None)
        if baseline is None:baseline=comparable
        assert comparable==baseline,(panel,ending,'interruption changed stroke')
        call(op='portrait_state',hover=point(.8,.8));assert wait()['recipe']==recipe
        call(op='portrait_state',paintPath=[point(.55,.6),point(.6,.6)])
        again=wait();assert again['history']==after['history']+1,(panel,ending,'next stroke failed')
        call(op='portrait_state',history=-1);assert wait()['recipe']==recipe
        records.append(dict(panel=panel,ending=ending,commitOnce=True,nextStroke=True,undo=True))
        call(op='portrait_close')
(OUT/'results.json').write_text(json.dumps(records,indent=2))
call(op='shutdown');print('Liquify, repair and hair: release/outside/Space/focus/capture-loss/window-deactivate passed.')
