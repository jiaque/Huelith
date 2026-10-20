from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-palette';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'workflow-settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png'
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
color=[.234567891,.123456789,.081234567];recipe['people'][0]['settings']['skinColor']=color
call(op='portrait_window',path=str(source),recipe=recipe,paletteEntries=[dict(name='legacy',hex='#c08060')]);wait()
call(op='portrait_state',select=0,panel=6);wait()
call(op='portrait_state',savePaletteName='precise sample');state=call(op='portrait_state',paletteState=True)
entries=state['paletteEntries'];assert len(entries)==2 and entries[1]['linearRec2020']==color and entries[1]['colorSpace']=='linear-rec2020'
call(op='portrait_state',paletteIndex=1);legacy=wait()['recipe'];assert legacy['people'][0]['settings']['skinColor']!=color
call(op='portrait_state',paletteIndex=2);state=wait();assert state['recipe']['people'][0]['settings']['skinColor']==color
call(op='portrait_state',history=-1);assert wait()['recipe']==legacy
call(op='portrait_state',history=1);wait()
call(op='portrait_state',click='portraitRemoveSkinColor');state=call(op='portrait_state',paletteState=True)
assert len(state['paletteEntries'])==1 and state['recipe']['people'][0]['settings']['skinColor']==color
saved=call(op='portrait_state',source=str(source),render=str(OUT/'result.png'),checkRegion=True,save=str(OUT/'palette.huelith'))
assert saved['roundtrip'] and saved['regionError']<2e-6
call(op='portrait_close');call(op='shutdown');print('Palette precision, legacy reuse, undo, deletion isolation and project save passed.')
