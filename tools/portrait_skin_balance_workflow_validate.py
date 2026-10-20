from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-skin-balance';call(op='init',settings=str(OUT/'workflow-settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png'
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=6);wait();before=wait()['recipe'];p=before['people'][0];x,y,w,h=p['face']
call(op='portrait_state',click='portraitBalanceReference');call(op='portrait_state',paint=[x+w*.5,y+h*.25]);chosen=wait()['recipe'];assert chosen['people'][0]['settings']['skinBalanceVersion']==2 and len(chosen['people'][0]['settings']['balanceReference'])==3
assert chosen['people'][0]['settings'].get('skinColor')==before['people'][0]['settings'].get('skinColor')
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==chosen
r=call(op='portrait_state',source=str(source),render=str(OUT/'manual.png'),checkRegion=True,save=str(OUT/'balance.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_state',click='portraitBalanceAutomatic');automatic=wait()['recipe'];assert 'balanceReference' not in automatic['people'][0]['settings'];call(op='portrait_state',history=-1);assert wait()['recipe']==chosen
call(op='portrait_state',click='portraitBalanceReference');call(op='portrait_state',paint=[.03,.9]);assert wait()['recipe']==chosen
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=chosen);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'manual.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Manual reference region, automatic reference, invalid region, undo, ROI and save passed')
