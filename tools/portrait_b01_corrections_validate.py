from pathlib import Path
exec(compile(Path('tools/portrait_b01_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b01/corrections';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'));wait()
 call(op='portrait_state',workspaceTask=3,select=0,panel=1);wait()
 call(op='portrait_state',click='portraitFeatureSide_eyes_1')
 call(op='portrait_state',parameter='feature.eyeSize',value=20);wait()
 call(op='portrait_state',parameter='feature.smile',value=30);s=wait()
 p=s['recipe']['people'][0]['settings']['parameters'];assert p['feature.eyeSize.left']==20 and p.get('feature.eyeSize.right',0)==0 and p['feature.smile.left']==p['feature.smile.right']==30,p
 call(op='portrait_state',click='portraitFeatureSide_mouth_2');call(op='portrait_state',parameter='feature.smile',value=15);s=wait()
 p=s['recipe']['people'][0]['settings']['parameters'];assert p['feature.smile.left']==30 and p['feature.smile.right']==15,p
 call(op='portrait_state',parameter='feature.browHeight',value=10);s=wait();p=s['recipe']['people'][0]['settings']['parameters'];assert p['feature.browHeight.left']==p['feature.browHeight.right']==10
 call(op='portrait_state',panel=2,parameter='head.size',value=30);s=wait();assert s['recipe']['people'][0]['settings']['parameters']['head.size']==-30
 before=s['recipe'];call(op='portrait_state',click='portraitLocate');lm=before['people'][0]['landmarks'];a=lm[8][:2];b=[a[0]+.002,a[1]-.002]
 call(op='portrait_state',stroke=[a,b,b]);s=wait();after=s['recipe'];assert after['people'][0]['settings']['faceSurface']['manuallyCorrected'];assert after['people'][0]['settings']['faceSurface']['points']!=before['people'][0]['settings']['faceSurface']['points']
 call(op='portrait_state',click='portraitLocate');call(op='portrait_state',parameter='head.yaw',value=25);s=wait();assert s['status'].startswith('已调整'),s['status']
 combined=s['recipe'];call(op='portrait_state',history=-1);wait();call(op='portrait_state',history=1);assert wait()['recipe']==combined
 saved=call(op='portrait_state',source=str(source),render=str(OUT/'combined.png'),save=str(OUT/'combined.huelith'),checkRegion=True);assert saved['roundtrip'] and saved['regionError']<2e-6
 call(op='portrait_state',capture=str(OUT/'window.png'));call(op='portrait_close');call(op='portrait_window',project=str(OUT/'combined.huelith'));assert wait()['recipe']==combined
 (OUT/'result.json').write_text(json.dumps({'independentSides':True,'smallHeadPositiveShrinks':True,'manualSurfaceSynchronized':True,'undoRedo':True,'roundtrip':saved['roundtrip'],'regionError':saved['regionError']},indent=2),encoding='utf-8')
 print('B01 correction workflow passed')
finally:call(op='portrait_close');call(op='shutdown')
