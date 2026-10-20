from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b03';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=9,select=0,portraitSlimAmount=20);s=wait();assert s['recipe']['people'][0]['settings']['parameters']['face.small']==20
 call(op='pump',waitMs=100);call(op='portrait_state',capture=str(OUT/'064.png'))
 call(op='portrait_state',workspaceTask=3,panel=0,parameter='face.width',value=-12);wait();call(op='portrait_state',workspaceTask=9,slimMode=1);wait()
 fx,fy,fw,fh=recipe['people'][0]['face'];start=np.array([fx+fw*.35,fy+fh*.6]);end=start+[fw*.15,0]
 seed=wait()['recipe'];baseline=field('baseline');records=[]
 for number,tool in [('065',0),('066',1),('067',5),('068',4),('069',3),('070',2),('071',6)]:
  call(op='portrait_state',liquifyTool=0,brushSize=180,portraitLiquifyPressure=50)
  call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);wait();before=field('seed-'+number)
  call(op='portrait_state',liquifyTool=tool);state=wait();history=state['history']
  path=[(start+(end-start)*t).tolist() for t in np.linspace(0,1,12)]
  call(op='portrait_state',paintPath=path,paintStepMs=30);state=wait();after=field('tool-'+number)
  change=float(np.max(np.linalg.norm((after-before)*(size-1),axis=2)));assert change>.001,(tool,change,state['status']);assert state['history']==history+1
  assert state['recipe']['people'][0]['settings']['parameters']['face.width']==-12
  call(op='pump',waitMs=100);call(op='portrait_state',hover=start.tolist(),capture=str(OUT/(number+'.png')))
  call(op='portrait_state',history=-1);undo=field('undo-'+number);assert np.max(np.abs(undo-before))<1e-6
  call(op='portrait_state',history=1);redo=field('redo-'+number);assert np.max(np.abs(redo-after))<1e-6
  records.append(dict(tool=tool,changedPixels=change));call(op='portrait_state',click='portraitLiquifyReset');restored=field('reset-'+number);assert np.max(np.abs(restored-baseline))<1e-6
 call(op='portrait_state',liquifyTool=0,brushSize=140);call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);before=field('freeze-before')
 call(op='portrait_state',click='portraitCanvasFreeze7',portraitLiquifyPressure=100,brushSize=500);call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);frozen=field('frozen');assert np.max(np.abs(frozen-before))<1e-6
 call(op='pump',waitMs=100);call(op='portrait_state',capture=str(OUT/'072.png'))
 call(op='portrait_state',liquifyTool=0,brushSize=100);call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);held=field('held');assert np.max(np.abs(held-frozen))<1e-6
 call(op='portrait_state',click='portraitCanvasFreeze8',brushSize=500);call(op='portrait_state',paintPath=[start.tolist(),end.tolist()]);thawed=field('thawed');assert np.max(np.abs(thawed-held))<1e-6
 call(op='pump',waitMs=100);call(op='portrait_state',capture=str(OUT/'073.png'))
 call(op='portrait_state',liquifyTool=0,brushSize=140);call(op='portrait_state',paintPath=[start.tolist(),end.tolist()],paintEnd='ungrab');continued=field('continued');assert np.max(np.abs(continued-thawed))>1e-5
 saved=wait()['recipe'];r=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'combined.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 call(op='portrait_close');call(op='portrait_window',project=str(OUT/'combined.huelith'));assert wait()['recipe']==saved
 call(op='portrait_close')
 directions=[]
 for reverse in [False,True]:
  call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=4,liquifyTool=3,brushSize=140);call(op='portrait_state',paint=start.tolist(),alt=reverse);directions.append(field('alt-'+str(reverse)));call(op='portrait_close')
 assert float(np.sum(directions[0]*directions[1]))<0
 bodySource=ROOT/'research/m3/portraits/jassi-standing.jpg';bodyRecipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text());bodyRecipe['people'][0]['settings']['parameters']={}
 call(op='portrait_window',path=str(bodySource),recipe=bodyRecipe);wait();call(op='portrait_state',workspaceTask=9,slimMode=0,click='portraitSlimCard1',portraitSlimAmount=100);s=wait();params=s['recipe']['people'][0]['settings']['parameters'];assert params['body.legWidth.left']==10 and params['body.legWidth.right']==10,params
 call(op='portrait_state',portraitSlimAmount=50);params=wait()['recipe']['people'][0]['settings']['parameters'];assert params['body.legWidth.left']==5,params
 call(op='portrait_state',portraitSlimAmount=0);params=wait()['recipe']['people'][0]['settings']['parameters'];assert params['body.legWidth.left']==0,params
 records.append({'automaticCombinationStrengthAndReset':True})
 (OUT/'result.json').write_text(json.dumps(records,indent=2),encoding='utf-8');print(records)
finally:call(op='portrait_close');call(op='shutdown')
