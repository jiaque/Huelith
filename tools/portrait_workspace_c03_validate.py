from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-c03';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=json.loads((source.parent/'leroy-chiao-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=1,select=0);wait()
 call(op='portrait_state',capture=str(OUT/'003.png'))
 call(op='portrait_state',hairPage=1);wait()
 for i in range(1,5):
  call(op='portrait_state',click=f'portraitHairCard{i}');s=wait();call(op='portrait_state',source=str(source),render=str(OUT/f'quality-{i}.png'))
 call(op='portrait_state',capture=str(OUT/'004.png'))
 call(op='portrait_state',hairPage=2);wait();call(op='portrait_state',hairColorFamily=1,click='portraitHairColor_9b4168');wait();call(op='portrait_state',capture=str(OUT/'005.png'))
 call(op='portrait_state',click='portraitHairRoots');wait();call(op='portrait_state',capture=str(OUT/'009.png'))
 for mode in (1,2,3):
  call(op='portrait_state',hairDyeMode=mode);wait()
  if mode==1:call(op='portrait_state',click='portraitHairAddZone');wait()
  call(op='portrait_state',click='portraitHairColor_534d96');wait()
  if mode!=2:call(op='portrait_state',brushSize=60,paintPath=[[.46,.16],[.51,.18]]);wait()
  call(op='portrait_state',capture=str(OUT/f'{5+mode:03}.png'))
 call(op='portrait_state',hairPage=3);wait();call(op='portrait_state',click='portraitDecalAdd_bow');s=wait();call(op='portrait_state',capture=str(OUT/'010.png'))
 assert len(s['recipe']['people'][0]['settings']['decals'])==1
 quad=np.array(s['recipe']['people'][0]['settings']['decals'][0]['quad']);center=quad.mean(0)
 call(op='portrait_state',paintPath=[center.tolist(),(center+[.02,.01]).tolist()]);s=wait();assert np.linalg.norm(np.array(s['recipe']['people'][0]['settings']['decals'][0]['quad'])-quad)>.01
 call(op='portrait_state',click='portraitDecalAction1',brushSize=60,paint=center.tolist());s=wait();id=s['recipe']['people'][0]['settings']['decals'][0]['id'];assert 'decal.'+id in s['recipe']['people'][0]['settings']['masks']
 call(op='portrait_state',click='portraitDecalAction0');wait();before=wait()['recipe'];call(op='portrait_state',click='portraitDecalAction2');wait();call(op='portrait_state',history=-1);assert wait()['recipe']==before
 r=call(op='portrait_state',source=str(source),save=str(OUT/'hair.huelith'),render=str(OUT/'saved.png'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
 saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'hair.huelith'));assert wait()['recipe']==saved
 print('C03 hair workflow passed')
finally:call(op='portrait_close');call(op='shutdown')
