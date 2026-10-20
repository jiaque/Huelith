from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],str(Path('tools/portrait_freeze_workflow_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/liquify-controls';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'path-settings'))
call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',select=0,panel=4,brushSize=180);wait()
fx,fy,fw,fh=recipe['people'][0]['face'];start=np.array([fx+fw*.4,fy+fh*.65]);end=start+[fw*.1,0]
call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);seed=wait()['recipe'];original=field('path-seed');call(op='portrait_close')
records=[]
for tool in [2,6]:
 fields=[]
 for count,delay in [(2,0),(41,0),(41,35)]:
  call(op='portrait_window',path=str(source),recipe=seed);wait()
  call(op='portrait_state',select=0,panel=4,brushSize=120,liquifyTool=tool);wait()
  path=[(start+(end-start)*t).tolist() for t in np.linspace(0,1,count)]
  call(op='portrait_state',paintPath=path,paintStepMs=delay);fields.append(field(f'tool-{tool}-{count}-{delay}'));call(op='portrait_close')
 error=max(float(np.max(np.linalg.norm((f-fields[0])*(size-1),axis=2))) for f in fields)
 change=float(np.max(np.linalg.norm((fields[0]-original)*(size-1),axis=2)))
 assert error<.05 and change>.001,(tool,error,change)
 records.append(dict(tool=tool,maximumPathDifferencePixels=error,effectiveChangePixels=change))
(OUT/'remaining-path.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(records)
