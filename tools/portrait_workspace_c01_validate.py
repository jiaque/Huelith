from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-c01';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads(source.with_name('jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=7,select=0,parameter='skin.oil',value=80);wait()
 call(op='portrait_state',parameter='skin.oilGloss',value=65);state=wait();call(op='portrait_state',capture=str(OUT/'049.png'),source=str(source),render=str(OUT/'automatic.png'))
 saved=state['recipe'];call(op='portrait_state',oilMode=1);assert wait()['recipe']==saved
 call(op='portrait_state',source=str(source),render=str(OUT/'manual-unchanged.png'));assert np.array_equal(np.array(Image.open(OUT/'automatic.png')),np.array(Image.open(OUT/'manual-unchanged.png')))
 fx,fy,fw,fh=recipe['people'][0]['face'];path=[[fx+fw*.4,fy+fh*.25],[fx+fw*.6,fy+fh*.25]]
 call(op='portrait_state',click='portraitOilErase',brushSize=90,paintPath=path);erased=wait();assert erased['recipe']!=saved
 call(op='portrait_state',history=-1);assert wait()['recipe']==saved
 call(op='portrait_state',history=1);assert wait()['recipe']==erased['recipe']
 call(op='portrait_state',click='portraitOilPaint',paintPath=path);painted=wait();assert painted['recipe']!=erased['recipe'];call(op='portrait_state',capture=str(OUT/'050.png'))
 call(op='portrait_state',oilMode=0);assert wait()['recipe']==painted['recipe']
 r=call(op='portrait_state',source=str(source),save=str(OUT/'oil.huelith'),render=str(OUT/'saved.png'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'oil.huelith'));assert wait()['recipe']==saved
 call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'saved.png')),np.array(Image.open(OUT/'reopened.png')))
 print('C01 shared coverage, preserve-gloss persistence, undo/redo, ROI and reopen passed')
finally:call(op='portrait_close');call(op='shutdown')
