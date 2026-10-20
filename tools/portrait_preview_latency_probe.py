"""Measure input handling separately from preview worker phases, offscreen."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],str(Path('tools/portrait_freeze_workflow_validate.py').resolve()),'exec'))
import platform
OUT=ROOT/'docs/test-evidence/M3/preview-geometry';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'latency-settings'));records=[]
for tool in [0,1]:
 call(op='portrait_window',project=str(ROOT/'docs/test-evidence/M3/liquify-freeze/freeze.huelith'));wait()
 call(op='portrait_state',select=0,panel=4,liquifyTool=tool,brushSize=140);state=wait()
 fx,fy,fw,fh=state['recipe']['people'][0]['face'];start=np.array([fx+fw*.4,fy+fh*.65]);end=start+[fw*.1,0]
 path=[(start+(end-start)*t).tolist() for t in np.linspace(0,1,41)]
 clock=time.monotonic();r=call(op='portrait_state',paintPath=path,paintStepMs=35,paintEnd='hold');wall=time.monotonic()-clock
 held=wait();clock=time.monotonic();call(op='portrait_state',paintRelease=end.tolist());settled=wait();settle=time.monotonic()-clock
 events=r['paintEventMilliseconds'];frames=r['paintFrames']
 records.append(dict(tool=tool,pathSeconds=wall,pressMs=events[0],moveP50Ms=float(np.percentile(events[1:],50)),moveP95Ms=float(np.percentile(events[1:],95)),moveMaxMs=max(events[1:]),framesDuringPath=max(frames)-min(frames),fastWorker=held['previewTimings'],fineWorker=settled['previewTimings'],settleSeconds=settle))
 call(op='portrait_close')
result=dict(machine=platform.processor(),sourceSize=size.tolist(),records=records)
(OUT/'latency.json').write_text(json.dumps(result,indent=2));call(op='shutdown');print(result)
