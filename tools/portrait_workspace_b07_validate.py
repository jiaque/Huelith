from pathlib import Path
import os,copy
tmp=Path.cwd()/'build/b07-temp';tmp.mkdir(exist_ok=True)
os.environ['TEMP']=os.environ['TMP']=str(tmp)
os.environ['HUELITH_AI_WORKER_EXE']=str(Path.cwd()/'build/windows-release/Huelith.exe')
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b07';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((source.parent/'jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={};recipe['people'][0]['settings']['mouthTopologyVersion']=2
fx,fy,fw,fh=recipe['people'][0]['face'];concealPath=[[fx+fw*x,fy+fh*.58] for x in [.18,.2,.22]]
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe,models=str(ROOT/'build/windows-release/models'));wait();call(op='portrait_state',workspaceTask=5,select=0,brushSize=12);wait();call(op='portrait_state',capture=str(OUT/'039.png'))
 call(op='portrait_state',blemishTool=1,click='portraitConcealColor2');wait();call(op='portrait_state',paintPath=concealPath);painted=wait();assert painted['recipe']['people'][0]['settings']['masks']['repair.conceal'];call(op='portrait_state',capture=str(OUT/'040.png'))
 call(op='portrait_state',click='portraitConcealErase',paintPath=concealPath);erased=wait();assert erased['recipe']!=painted['recipe'];call(op='portrait_state',history=-1);assert wait()['recipe']==painted['recipe'];call(op='portrait_state',history=1);wait()
 call(op='portrait_state',blemishTool=2);wait();cases=json.loads((ROOT/'docs/test-evidence/M3/semantic-makeup-repair/cases.json').read_text())
 for case in cases:
  part={'lip':0,'brow':1,'shadow':2}[case['part'].split('.')[0]];call(op='portrait_state',click='portraitMakeupRepairPart'+str(part));before=wait();call(op='portrait_state',paint=case['target']);state=wait();assert len(state['recipe']['people'][0]['settings'].get('repairs',[]))==len(before['recipe']['people'][0]['settings'].get('repairs',[]))+1,(case,state['status']);assert state['recipe']['people'][0]['settings']['repairs'][-1]['makeupPart']==case['part']
 call(op='portrait_state',capture=str(OUT/'041.png'));saved=wait();call(op='portrait_state',paint=[.5,.05],alt=True);call(op='portrait_state',paint=cases[-1]['target']);assert wait()['recipe']==saved['recipe']
 r=call(op='portrait_state',source=str(source),render=str(OUT/'manual.png'),save=str(OUT/'manual.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 call(op='portrait_state',blemishPage=1);wait();call(op='portrait_state',capture=str(OUT/'042.png'));call(op='portrait_close')
 # Explicit empty/failure state fixtures, without claiming emptiness means flawless skin.
 empty=copy.deepcopy(recipe);empty['people'][0]['settings']['blemishDetectionComplete']=True;empty['people'][0]['settings']['blemishes']=[]
 call(op='portrait_window',path=str(source),recipe=empty,models=str(OUT/'missing-model'));wait();call(op='portrait_state',workspaceTask=5,select=0,blemishPage=1);wait();call(op='portrait_state',capture=str(OUT/'044.png'));before=wait()['recipe'];call(op='portrait_state',click='portraitAnalyzeRepairs');state=wait();assert state['recipe']==before and '模型缺失' in state['status'];call(op='portrait_state',capture=str(OUT/'045.png'));call(op='portrait_close')
 source=ROOT/'docs/test-evidence/M3/quality/acne-face-source.png';recipe=json.loads((source.parent/'acne-face-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={};recipe['people'][0]['settings']['blemishes']=[]
 call(op='portrait_window',path=str(source),recipe=recipe,models=str(ROOT/'build/windows-release/models'));wait();call(op='portrait_state',workspaceTask=5,select=0,blemishPage=1);before=wait();call(op='portrait_state',click='portraitAnalyzeRepairs');state=wait();assert state['status']=='范围分析完成',state['status'];marks=state['recipe']['people'][0]['settings']['blemishes'];assert marks and not state['recipe']['people'][0]['settings'].get('repairs');call(op='portrait_state',capture=str(OUT/'043.png'))
 call(op='portrait_state',blemishSelect=0,click='portraitBlemishKeep');state=wait();assert state['recipe']['people'][0]['settings']['blemishes'][0]['state']=='preserved'
 call(op='portrait_state',click='portraitAnalyzeRepairs');state=wait();assert state['status']=='范围分析完成',state['status'];assert state['recipe']['people'][0]['settings']['blemishes'][0]['state']=='preserved'
 call(op='portrait_state',blemishSelect=1,click='portraitBlemishRepair');state=wait();assert state['recipe']['people'][0]['settings'].get('repairs'),state['status']
 before=state['recipe'];call(op='portrait_state',click='portraitAnalyzeRepairs');call(op='portrait_state',cancelCompute=True);assert wait()['recipe']==before
 r=call(op='portrait_state',source=str(source),render=str(OUT/'suggestions.png'),save=str(OUT/'suggestions.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 (OUT/'result.json').write_text(json.dumps({'manualTools':True,'samePartSource':True,'fourStates':True,'detectDoesNotErase':True,'refreshPreservesDecision':True,'cancelPreservesRecipe':True,'save':True},indent=2));print('B07 group passed')
finally:call(op='portrait_close');call(op='shutdown')
