"""Focused offscreen density check; no image algorithm regression."""
from pathlib import Path
exec(compile(Path('tools/portrait_b01_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/compact-layout'/os.environ.get('QT_SCALE_FACTOR','1');OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
try:
 recipe=json.loads((ROOT/'docs/test-evidence/M3/workspace-b01/orientation/chris-hadfield-recipe.json').read_text(encoding='utf-8'))
 recipe['people'][0]['settings']['parameters']={}
 call(op='portrait_window',path=str(ROOT/'docs/test-evidence/M3/quality/chris-hadfield-source.png'),recipe=recipe,models=str(ROOT/'build/windows-release/models'));wait()
 records=[]
 for width,height in [(1600,1000),(1280,800)]:
  call(op='portrait_state',resize=[width,height],workspaceTask=3,select=0);wait()
  first=None
  for task,panel in [(3,0),(3,1),(3,2),(4,5),(9,4)]:
   call(op='portrait_state',workspaceTask=task,panel=panel);wait()
   state=call(op='portrait_state',workspaceLayout=True,capture=str(OUT/f'{width}-task{task}-panel{panel}.png'))
   layout=state['workspaceLayout'];assert len(state['workspaceTasks'])==15
   assert layout['portraitBottomBar'][3]==52
   assert layout['portraitInspector'][2]==(448 if width==1600 else 400)
   assert layout['portraitPeopleBar'][3]==52
   if first is None:first=layout['portraitView']
   assert first==layout['portraitView'],'page switch changed canvas allocation'
   records.append({'width':width,'task':task,'panel':panel,'layout':layout})
 (OUT/'result.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
 print('Compact layout: five representative pages at two window sizes passed')
finally:call(op='shutdown')
