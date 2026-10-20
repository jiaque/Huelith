import os
from pathlib import Path
ROOT=Path.cwd();temp=ROOT/'build/portrait-ai-tmp';temp.mkdir(exist_ok=True)
os.environ['TMP']=os.environ['TEMP']=str(temp);os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe')
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-red-eye';call(op='init',settings=str(OUT/'settings'))
def wait():
    end=time.monotonic()+90
    while time.monotonic()<end:
        call(op='pump',waitMs=30);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy'] and r['count']>0:return r
    raise AssertionError(r)
records=[]
for name in ['red-eye','jessica-watkins','leroy-chiao']:
    source=OUT/'red-eye-source.jpg' if name=='red-eye' else ROOT/f'docs/test-evidence/M3/quality/{name}-source.png'
    kwargs={'models':str(ROOT/'build/windows-release/models')} if name=='red-eye' else {'recipe':json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())}
    if 'recipe' in kwargs:
        settings=kwargs['recipe']['people'][0]['settings'];settings['parameters']={}
        for key in list(settings['automaticMasks']):
            if key.startswith('eye.'):del settings['automaticMasks'][key]
    call(op='portrait_window',path=str(source),**kwargs);wait();call(op='portrait_state',select=0,panel=9);state=wait();before=state['recipe']
    call(op='portrait_state',parameter='eye.red',value=100);state=wait();assert state['recipe']['people'][0]['settings']['redEyeVersion']==2
    call(op='portrait_state',history=-1);assert wait()['recipe']==before
    call(op='portrait_state',history=1);state=wait();recipe=state['recipe']
    saved=call(op='portrait_state',source=str(source),render=str(OUT/f'{name}-workflow.png'),checkRegion=True,save=str(OUT/f'{name}.huelith'));assert saved['roundtrip'] and saved['regionError']<2e-6
    call(op='portrait_close');size=list(Image.open(source).size);(OUT/f'{name}-recipe.json').write_text(json.dumps(recipe))
    r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(OUT/f'{name}-corrected.png'),colorMetrics=True,styleMask='eye.red',mask=str(OUT/f'{name}-mask.png'))
    mask=np.asarray(Image.open(OUT/f'{name}-mask.png'));records.append(dict(name=name,selected=int((mask>40).sum()),**r))
    if name=='red-eye':assert (mask>40).sum()>10,records[-1]
    else:assert (mask>40).sum()==0,records[-1]
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(records)

