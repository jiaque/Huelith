from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/semantic-pelvis';OUT.mkdir(exist_ok=True)
tmp=ROOT/'build/portrait-ai-tmp';tmp.mkdir(exist_ok=True)
os.environ['TMP']=os.environ['TEMP']=str(tmp)
os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe')
call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['jassi-standing']:
 source=ROOT/'research/m3/portraits'/f'{name}.jpg';size=np.array(Image.open(source).size)
 call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'))
 end=time.monotonic()+120
 while time.monotonic()<end:
  call(op='pump',waitMs=20);state=call(op='portrait_state')
  if state['ready'] and not state['previewBusy']:break
 else:raise AssertionError('recognition timeout')
 recipe=state['recipe'];call(op='portrait_close')
 (OUT/f'{name}-recipe.json').write_text(json.dumps(recipe))
 for index,p in enumerate(recipe['people']):
  for side in ['left','right']:
   trial=copy.deepcopy(recipe);trial['people'][index]['settings']['parameters']={'body.hipWidth.'+side:100}
   try:
    call(op='portrait_geometry',recipe=trial,size=size.tolist(),source=str(source),field=str(OUT/'production-field.bin'),render=str(OUT/f'{name}-{index}-{side}.png'))
    records.append(dict(name=name,person=index,side=side,rendered=True))
   except Exception as e:records.append(dict(name=name,person=index,side=side,rendered=False,reason=str(e)))
 print(name,records[-2:],flush=True)
(OUT/'standing-probe.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
call(op='shutdown')
