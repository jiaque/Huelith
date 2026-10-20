from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-smooth-styles';call(op='init',settings=str(OUT/'workflow-settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png'
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',select=0,panel=5);wait()

initial=wait()['recipe'];last=initial
for style in ['classic','texture','native']:
    before=wait();call(op='portrait_state',click='portraitSmoothStyle_'+style);state=wait();settings=state['recipe']['people'][0]['settings'];assert settings['smoothStyle']==style
    assert settings['parameters']['skin.smooth']==40
    assert settings.get('masks')==before['recipe']['people'][0]['settings'].get('masks')
    assert state['zoom']==before['zoom']
    call(op='portrait_state',history=-1);assert wait()['recipe']==before['recipe']
    call(op='portrait_state',history=1);assert wait()['recipe']==state['recipe']
    r=call(op='portrait_state',source=str(source),render=str(OUT/(style+'.png')),checkRegion=True,save=str(OUT/(style+'.huelith')));assert r['roundtrip'] and r['regionError']<2e-6
    saved=call(op='portrait_geometry',portableSkin=settings,targetSettings={});assert saved['portable']['smoothStyle']==style and saved['merged']['smoothStyle']==style
    last=state['recipe']
legacy=call(op='portrait_geometry',portableSkin={'parameters':{'skin.smooth':30}},targetSettings=last['people'][0]['settings']);assert 'smoothStyle' not in legacy['merged']
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=last);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'native.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Three style controls: one undo, stable scope/zoom, ROI, project and preset roundtrip passed')
