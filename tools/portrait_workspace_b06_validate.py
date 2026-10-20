from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b06';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/john-glenn-source.png';size=np.array(Image.open(source).size);recipe=json.loads((source.parent/'john-glenn-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=10,select=0,brushSize=22);before=wait()
 path=[[x/943,(249-4*(x-335)/70)/1200] for x in range(335,406,5)]
 call(op='portrait_state',paintPath=path,paintEnd='ungrab');s=wait();strokes=s['recipe']['people'][0]['settings']['repairs'];assert len(strokes)==1 and len(strokes[0]['points'])>3 and not strokes[0].get('clone',False);assert s['history']==before['history']+1
 call(op='portrait_state',capture=str(OUT/'074.png'));call(op='portrait_state',history=-1);assert not wait()['recipe']['people'][0]['settings'].get('repairs');call(op='portrait_state',history=1);assert wait()['recipe']==s['recipe']
 call(op='portrait_state',eraseMode=1);before=wait();call(op='portrait_state',paint=path[0]);assert wait()['history']==before['history']
 samplePoint=[path[0][0],path[0][1]-.025];call(op='portrait_state',paint=samplePoint,alt=True);call(op='portrait_state',paintPath=path);first=wait();stroke=first['recipe']['people'][0]['settings']['repairs'][-1];assert stroke['clone'] and stroke['kind']=='heal'
 offset=[stroke['offsetX'],stroke['offsetY']];call(op='portrait_state',paintPath=[[p[0],p[1]+.015] for p in path]);s=wait();last=s['recipe']['people'][0]['settings']['repairs'][-1];assert last['clone'] and abs(last['offsetX']-offset[0])+abs(last['offsetY']-offset[1])<1e-8
 call(op='portrait_state',hover=path[-1],capture=str(OUT/'075.png'))
 beforeInvalid=wait();call(op='portrait_state',paint=[.999,.2],alt=True);call(op='portrait_state',paintPath=[path[0],path[-1]]);invalid=wait();assert invalid['history']==beforeInvalid['history'] and '超出' in invalid['status']
 r=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'repair.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'repair.huelith'));assert wait()['recipe']==saved
 # Same source and stroke must visibly distinguish clone from blended healing.
 import copy
 only=copy.deepcopy(saved);only['people'][0]['settings']['repairs']=[stroke];renders=[]
 for clone in [True,False]:
  only['people'][0]['settings']['repairs'][0]['clone']=clone;pathOut=OUT/('clone.png' if clone else 'heal.png');call(op='portrait_geometry',source=str(source),recipe=only,size=size.tolist(),render=str(pathOut));renders.append(np.array(Image.open(pathOut)).astype(int))
 assert np.max(abs(renders[0]-renders[1]))>0
 (OUT/'result.json').write_text(json.dumps({'continuousRepair':True,'alignedClone':True,'cloneDiffersFromHeal':True,'roundtrip':True},indent=2));print('B06 group passed')
finally:call(op='portrait_close');call(op='shutdown')
