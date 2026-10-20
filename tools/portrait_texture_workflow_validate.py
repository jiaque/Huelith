from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-texture';call(op='init',settings=str(OUT/'workflow-settings'))
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
for key in ['skin.texture']:
    before=wait()['recipe'];call(op='portrait_state',parameter=key,value=40);state=wait()
    assert state['recipe']['people'][0]['settings']['textureVersion']==2
    call(op='portrait_state',history=-1);assert wait()['recipe']==before
    call(op='portrait_state',history=1);wait()
    call(op='portrait_state',doubleClickParameter=key);state=wait()
    assert state['recipe']['people'][0]['settings']['parameters'].get(key,0)==0
call(op='portrait_state',parameter='skin.texture',value=65);wait()
call(op='portrait_state',parameter='skin.texture',value=50);state=wait()
saved=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'chroma.huelith'))
assert saved['roundtrip'] and saved['regionError']<2e-6
recipe=state['recipe'];call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',source=str(source),render=str(OUT/'workflow-reopened.png'))
assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'workflow-reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Texture control, undo, reset, regions and reopening passed.')
