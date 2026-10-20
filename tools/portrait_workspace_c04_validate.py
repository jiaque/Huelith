from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/workspace-c04';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=json.loads((source.parent/'leroy-chiao-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=12,select=0);wait()
 for mode,key in enumerate(['paint.highlight','paint.makeup','paint.skin','paint.glitter','hair.color']):
  call(op='portrait_state',paintType=mode);wait();path=[[.48,.33],[.53,.33]] if mode!=4 else [[.47,.19],[.54,.19]]
  call(op='portrait_state',brushSize=65,paintPath=path);s=wait();assert key in s['recipe']['people'][0]['settings']['masks'],s['status']
  call(op='portrait_state',capture=str(OUT/f'{78+mode:03}.png'))
  before=s['recipe']['people'][0]['settings']['masks'][key]
  call(op='portrait_state',click='portraitPaintLock');wait();call(op='portrait_state',paintPath=[[.44,.32],[.49,.35]]);s=wait();assert s['recipe']['people'][0]['settings']['masks'][key]==before
  call(op='portrait_state',parameter=key,value=75);s=wait();assert s['recipe']['people'][0]['settings']['masks'][key]==before
  call(op='portrait_state',click='portraitPaintLock');wait()
 # Erasure modifies only the selected material.
 before=wait()['recipe']['people'][0]['settings']['masks'];call(op='portrait_state',paintType=1);wait();call(op='portrait_state',click='portraitPaintErase',paintPath=[[.48,.33],[.53,.33]]);s=wait();after=s['recipe']['people'][0]['settings']['masks'];assert after['paint.makeup']!=before['paint.makeup'];assert all(v==after[k] for k,v in before.items() if k!='paint.makeup')
 call(op='portrait_state',paintType=5);wait();call(op='portrait_state',click='portraitPaintAuto0');wait();call(op='portrait_state',capture=str(OUT/'083.png'));before=wait()['recipe'];call(op='portrait_state',paintLightMode=1);wait();assert wait()['recipe']==before,'switching manual light duplicates automatic light'
 call(op='portrait_state',click='portraitPaintDark',brushSize=45,paintPath=[[.47,.34],[.5,.36]]);s=wait();assert 'paint.dark' in s['recipe']['people'][0]['settings']['masks'];call(op='portrait_state',capture=str(OUT/'084.png'))
 before=wait()['recipe'];call(op='portrait_state',workspaceTask=3);wait();call(op='portrait_state',workspaceTask=12);assert wait()['recipe']==before
 r=call(op='portrait_state',source=str(source),save=str(OUT/'paint.huelith'),render=str(OUT/'saved.png'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
 saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'paint.huelith'));assert wait()['recipe']==saved
 print('C04 seven pages, paint activation, independent locks/erasure, automatic/manual light sharing, cross-entry and saving passed')
finally:call(op='portrait_close');call(op='shutdown')
