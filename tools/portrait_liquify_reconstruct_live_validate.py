from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],str(Path('tools/portrait_freeze_workflow_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/liquify-reconstruct'
call(op='init',settings=str(OUT/'live-settings'))
fields=[];records=[]
for ending in ['release','ungrab']:
 call(op='portrait_window',project=str(ROOT/'docs/test-evidence/M3/liquify-freeze/freeze.huelith'));wait()
 call(op='portrait_state',select=0,panel=4,liquifyTool=1,brushSize=140);before=field('live-before');history=wait()['history']
 fx,fy,fw,fh=wait()['recipe']['people'][0]['face'];start=np.array([fx+fw*.4,fy+fh*.65]);end=start+[fw*.12,0]
 path=[(start+(end-start)*t).tolist() for t in np.linspace(0,1,41)]
 start_time=time.monotonic();r=call(op='portrait_state',paintPath=path,paintStepMs=35,paintEnd=ending);elapsed=time.monotonic()-start_time
 after=field('live-'+ending);state=wait()
 assert max(r['paintFrames'][:-1])>r['paintFrames'][0] and state['history']==history+1,(r['paintFrames'],state['history'],history)
 call(op='portrait_state',history=-1);undo=field('live-undo')
 assert float(np.max(np.linalg.norm((undo-before)*(size-1),axis=2)))<.05
 call(op='portrait_state',history=1);redo=field('live-redo')
 assert float(np.max(np.linalg.norm((redo-after)*(size-1),axis=2)))<.05
 fields.append(after);records.append(dict(ending=ending,framesDuringDrag=r['paintFrames'],pathSeconds=elapsed));call(op='portrait_close')
error=float(np.max(np.linalg.norm((fields[0]-fields[1])*(size-1),axis=2)));assert error<.05,error
(OUT/'live.json').write_text(json.dumps(dict(runs=records,captureLossDifferencePixels=error),indent=2));call(op='shutdown');print(records,error)
