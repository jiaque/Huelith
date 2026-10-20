"""Temporary hand ownership through key-up, mouse-up and capture loss."""
from pathlib import Path
exec(compile(Path('tools/portrait_brush_target_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0], 'tools/portrait_brush_target_validate.py', 'exec'))
OUT=ROOT/'docs/test-evidence/M3/brush-capture';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'navigation-settings'));records=[]
for ending in ['release','ungrab','deactivate','focus']:
    call(op='portrait_window',path=str(source),recipe=base);wait()
    call(op='portrait_state',select=0,panel=4,click='portraitFocus3');before=wait()
    call(op='portrait_state',viewSpace=True)
    call(op='portrait_state',navigationMouse=['press',450,400])
    r=call(op='portrait_state',navigationMouse=['move',450,450])
    assert r['navigation']['temporary']
    first=r['navigation']['center']
    r=call(op='portrait_state',viewSpace=False)
    assert r['navigation']['temporary'],'Space-up returned ownership before mouse-up'
    r=call(op='portrait_state',navigationMouse=['move',450,500])
    assert np.linalg.norm(np.array(r['navigation']['center'])-first)>10,'hand no longer pans after Space-up'
    assert r['recipe']==before['recipe'] and r['history']==before['history'],'navigation painted'
    if ending=='release':r=call(op='portrait_state',navigationMouse=['release',-100,-100])
    elif ending=='focus':r=call(op='portrait_state',viewFocusOut=True)
    else:r=call(op='portrait_state',navigationEnd=ending)
    assert not r['navigation']['temporary'],(ending,'hand stuck')
    # Return the face to the viewport without changing the active brush.
    call(op='portrait_state',click='portraitFocus0')
    call(op='portrait_state',paintPath=[point(.4,.6),point(.55,.6)])
    after=wait();assert after['history']==before['history']+1,(ending,'next brush failed',before['history'],after['history'],after['status'],after['navigation'])
    call(op='portrait_state',history=-1);assert wait()['recipe']==before['recipe']
    records.append(dict(ending=ending,panAfterSpaceUp=True,noAccidentalPaint=True,brushRestored=True))
    call(op='portrait_close')
(OUT/'navigation.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(records)
