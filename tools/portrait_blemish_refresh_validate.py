from pathlib import Path
import os
os.environ['HUELITH_AI_WORKER_EXE']=str(Path(__file__).resolve().parents[1]/'build/windows-release/Huelith.exe')
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-blemish-categories';call(op='init',settings=str(OUT/'workflow-settings'))
source=ROOT/'docs/test-evidence/M3/quality/acne-face-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/acne-face-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={}
p['settings']['blemishes']=[dict(x=.28,y=.09,radius=.03,state='preserved'),dict(x=.55,y=.34,radius=.03,state='repaired'),dict(x=.99,y=.99,radius=.001,state='pending')]
def wait(analysis=False):
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=30);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy'] and (not analysis or r['status']=='范围分析完成'):return r
    raise AssertionError(r)
call(op='portrait_window',path=str(source),recipe=recipe,models=str(ROOT/'build/windows-release/models'));wait();call(op='portrait_state',select=0,panel=7);before=wait()['recipe']
call(op='portrait_state',click='portraitAnalyzeRepairs');after=wait(True)['recipe'];marks=after['people'][0]['settings']['blemishes']
assert marks[:2]==p['settings']['blemishes'][:2] and len(marks)>2
assert not any(q['x']==.99 for q in marks)
assert all(q['state']=='pending' and q['analysisVersion'] in [2,3,4] for q in marks[2:])
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
r=call(op='portrait_state',source=str(source),render=str(OUT/'refreshed.png'),save=str(OUT/'refreshed.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_state',click='portraitAnalyzeRepairs');again=wait(True)['recipe'];assert again['people'][0]['settings']['blemishes']==marks
call(op='portrait_state',click='portraitAnalyzeRepairs')
call(op='portrait_state',cancelCompute=True)
end=time.monotonic()+10
while time.monotonic()<end:
    call(op='pump',waitMs=25);cancelled=call(op='portrait_state')
    if cancelled['ready'] and '取消' in cancelled['status']:break
else:raise AssertionError('cancel did not settle')
assert cancelled['recipe']==again,'cancel changed settings'
call(op='portrait_state',click='portraitAnalyzeRepairs')
assert wait(True)['recipe']['people'][0]['settings']['blemishes']==marks,'cancel poisoned next request'
call(op='portrait_close');call(op='shutdown');print('Refresh replaces stale candidates, preserves decisions, is repeatable and undoable; save passed')
