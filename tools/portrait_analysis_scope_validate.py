"""Unrelated automatic range analysis must preserve blemish decisions and proposals."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe')
OUT=ROOT/'docs/test-evidence/M3/blemish-model/scope';OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/acne-face-source.png'
recipe=json.loads(source.with_name('acne-face-recipe.json').read_text())
marks=json.loads((OUT.parent/'fusion-results.json').read_text())['candidates']
recipe['people'][0]['settings']['blemishes']=marks
recipe['people'][0]['settings'].pop('hairLighting',None)
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=30);state=call(op='portrait_state')
        if state['ready'] and not state['previewBusy']:return state
    raise AssertionError(state['status'])
call(op='init',settings=str(OUT/'settings'))
call(op='portrait_window',path=str(source),recipe=recipe,models=str(OUT/'missing-models'));wait()
call(op='portrait_state',select=0,panel=12);state=wait()
assert state['recipe']['people'][0]['settings']['blemishes']==marks
assert state['recipe']['people'][0]['settings']['hairLighting']['flowVersion']>=2
call(op='portrait_state',panel=7);before=wait()['recipe']
call(op='portrait_state',click='portraitAnalyzeRepairs');state=wait()
assert '模型缺失' in state['status'] and state['recipe']==before
call(op='portrait_close')
bad=OUT/'bad-models';bad.mkdir(exist_ok=True);(bad/'blemish_detection.onnx').write_bytes(b'invalid')
call(op='portrait_window',path=str(source),recipe=before,models=str(bad));wait()
call(op='portrait_state',select=0,panel=7);before=wait()['recipe']
call(op='portrait_state',click='portraitAnalyzeRepairs');state=wait()
assert '校验失败' in state['status'] and state['recipe']==before
(OUT/'results.json').write_text(json.dumps(dict(unrelatedAnalysisPreservesCandidates=True,unrelatedAnalysisNeedsNoBlemishModel=True,missingModelReported=True,corruptModelReported=True,failedAnalysisLeavesRecipeUnchanged=True),indent=2))
call(op='portrait_close');call(op='shutdown');print('Scope isolation and missing/corrupt model UI recovery passed.')
