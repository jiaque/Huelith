from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-eye-shared';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=9);wait();before=wait()['recipe']
call(op='portrait_state',styleSection=2);call(op='portrait_state',click='portraitMaterialCard_catchMaterial_window');r=wait();assert r['panel']==9
chosen=r['recipe'];p=chosen['people'][0]['settings']['parameters'];assert p['makeup.catchlight.left']==70 and p['makeup.catchlight.right']==70
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==chosen
call(op='portrait_state',source=str(source),render=str(OUT/'eye-entry.png'))
call(op='portrait_state',panel=11);call(op='portrait_state',styleSection=4);assert wait()['recipe']==chosen
call(op='portrait_state',source=str(source),render=str(OUT/'makeup-entry.png'));assert np.array_equal(np.array(Image.open(OUT/'eye-entry.png')),np.array(Image.open(OUT/'makeup-entry.png')))
call(op='portrait_state',parameter='makeup.catchX',value=20);wait();call(op='portrait_state',panel=9);call(op='portrait_state',styleSection=2);call(op='portrait_state',doubleClickParameter='makeup.catchX');assert wait()['recipe']['people'][0]['settings']['parameters']['makeup.catchX.left']==0
call(op='portrait_state',styleSection=1);call(op='portrait_state',parameter='makeup.iris',value=55);call(op='portrait_state',click='portraitSwatch_755647');shared=wait()['recipe']
assert shared['people'][0]['settings']['parameters']['makeup.iris.left']==55 and 'makeup.iris' in shared['people'][0]['settings']['colors']
call(op='portrait_state',source=str(source),render=str(OUT/'iris-eye.png'));call(op='portrait_state',panel=11);call(op='portrait_state',styleSection=2);assert wait()['recipe']==shared
r=call(op='portrait_state',source=str(source),render=str(OUT/'iris-makeup.png'),save=str(OUT/'shared.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
assert np.array_equal(np.array(Image.open(OUT/'iris-eye.png')),np.array(Image.open(OUT/'iris-makeup.png')))
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=shared);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'iris-eye.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_state',select=0,panel=9);call(op='portrait_state',styleSection=1)
call(op='portrait_state',linked=False,side=0,parameter='makeup.iris',value=20);wait()
call(op='portrait_state',side=1,parameter='makeup.iris',value=60);unequal=wait()['recipe']['people'][0]['settings']['parameters']
call(op='portrait_state',linked=True);assert wait()['recipe']['people'][0]['settings']['parameters']==unequal
call(op='portrait_state',parameter='makeup.iris',value=35);tied=wait()['recipe']['people'][0]['settings']['parameters'];assert tied['makeup.iris.left']==35 and tied['makeup.iris.right']==35
call(op='portrait_state',history=-1);assert wait()['recipe']['people'][0]['settings']['parameters']==unequal
call(op='portrait_close');call(op='shutdown');print('In-place eye materials, shared settings and identical rendering, undo, reset, ROI and reopening passed')
