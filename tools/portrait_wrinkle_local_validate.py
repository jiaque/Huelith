from pathlib import Path
import sys
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'build/wrinkle-local';OUT.mkdir(exist_ok=True)
phase=sys.argv[1]
source=ROOT/'docs/test-evidence/M3/quality/john-glenn-source.png'
recipe=json.loads(source.with_name('john-glenn-recipe.json').read_text(encoding='utf-8'))
recipe['people'][0]['settings']['parameters']={};recipe['people'][0]['settings']['repairs']=[]
cases={'forehead':(22,[(x,249-4*(x-335)/70) for x in range(335,406,5)]),'eye':(10,[(305,311),(300,315),(295,319)]),'fold':(10,[(413,341),(420,346),(425,353),(429,361)])}
records=[]
call(op='init',settings=str(OUT/'settings'))
try:
 for name,(diameter,points) in cases.items():
  call(op='portrait_window',path=str(source),recipe=recipe);wait()
  call(op='portrait_state',workspaceTask=6,select=0,brushSize=diameter);wait()
  call(op='portrait_state',paintPath=[[x/943,y/1200] for x,y in points]);state=wait()
  strokes=state['recipe']['people'][0]['settings'].get('repairs',[])
  assert len(strokes)==1,(name,state['status'])
  stroke=strokes[0];offsets=np.array(stroke.get('sourceOffsets',[[stroke['offsetX'],stroke['offsetY']]]))*[943,1200]
  distance=float(np.linalg.norm(offsets,axis=1).max())
  if phase=='after':
   assert distance<=diameter*3+.01,(name,distance,diameter)
   if len(offsets)>1:assert np.linalg.norm(np.diff(offsets,axis=0),axis=1).max()<=diameter*.25+.01

  call(op='portrait_state',source=str(source),render=str(OUT/f'{phase}-{name}.png'))
  records.append(dict(name=name,maxSourceDistance=distance,diameter=diameter,status=state['status']))
  if phase=='after' and name=='eye':
   saved=state['recipe']
   call(op='portrait_state',paint=[.03,.9]);failed=wait()
   assert failed['recipe']==saved, 'Failed source changed prior work'
   assert 'Alt' in failed['status'],failed['status']
   call(op='portrait_state',paint=[.45,.2],alt=True);wait()
   call(op='portrait_state',paint=[305/943,311/1200]);manual=wait()
   repairs=manual['recipe']['people'][0]['settings']['repairs']
   assert len(repairs)==2,manual['status']
   assert repairs[0]==saved['people'][0]['settings']['repairs'][0]
   assert np.linalg.norm(np.array([repairs[-1]['offsetX'],repairs[-1]['offsetY']])*[943,1200])>diameter*3
  call(op='portrait_close')
 (OUT/f'{phase}.json').write_text(json.dumps(records,indent=2),encoding='utf-8');print(records)
finally:call(op='portrait_close');call(op='shutdown')
