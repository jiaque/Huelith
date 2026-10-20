from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b05';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';size=np.array(Image.open(source).size)
recipe=json.loads((source.parent/'jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=4,select=0,parameter='skin.smooth',value=70);wait();images=[]
 for style in ['classic','texture','native']:
  call(op='portrait_state',click='portraitSmoothStyle_'+style);state=wait();out=OUT/(style+'.png');call(op='portrait_state',previewImage=str(out));images.append(np.array(Image.open(out)).astype(float))
 assert all(np.max(abs(images[i]-images[j]))>0 for i,j in [(0,1),(1,2),(0,2)])
 call(op='portrait_state',parameter='skin.retention',value=50);wait();call(op='portrait_state',capture=str(OUT/'036.png'))
 call(op='portrait_state',skinMode=1);before=wait();fx,fy,fw,fh=recipe['people'][0]['face'];path=[[fx+fw*.25,fy+fh*.6],[fx+fw*.4,fy+fh*.68]]
 call(op='portrait_state',click='portraitSmoothErase',brushSize=100,paintPath=path);erased=wait();assert 'skin.smooth' in erased['recipe']['people'][0]['settings']['masks'];assert erased['history']==before['history']+1
 call(op='portrait_state',history=-1);assert wait()['recipe']==before['recipe'];call(op='portrait_state',history=1);assert wait()['recipe']==erased['recipe']
 call(op='portrait_state',click='portraitSmoothPaint',paintPath=path);painted=wait();assert painted['recipe']['people'][0]['settings']['masks']['skin.smooth']!=erased['recipe']['people'][0]['settings']['masks']['skin.smooth']
 call(op='portrait_state',capture=str(OUT/'037.png'))
 call(op='portrait_state',skinMode=2,click='portraitSkinSwatch2');state=wait();assert state['recipe']['people'][0]['settings']['parameters']['color.custom']==50
 call(op='portrait_state',parameter='color.balance',value=30);wait();call(op='portrait_state',capture=str(OUT/'038.png'))
 call(op='portrait_state',skinMode=0);state=wait();assert state['recipe']['people'][0]['settings']['masks']['skin.smooth']==painted['recipe']['people'][0]['settings']['masks']['skin.smooth']
 call(op='portrait_state',parameter='skin.bodySmooth',value=35);state=wait();assert state['recipe']['people'][0]['settings']['parameters']['skin.smooth']==70
 r=call(op='portrait_state',source=str(source),save=str(OUT/'skin.huelith'),render=str(OUT/'saved.png'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'skin.huelith'));assert wait()['recipe']==saved
 (OUT/'result.json').write_text(json.dumps({'stylesDistinct':True,'manualCoveragePreserved':True,'historyAndSave':True},indent=2));print('B05 group passed')
finally:call(op='portrait_close');call(op='shutdown')
