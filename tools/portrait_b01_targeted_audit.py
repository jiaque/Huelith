from pathlib import Path
exec(compile(Path('tools/portrait_b01_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b01/audit';call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(ROOT/'docs/test-evidence/M3/quality/chris-hadfield-source.png'),models=str(ROOT/'build/windows-release/models'));wait()
 call(op='portrait_state',workspaceTask=3,select=0,panel=1);wait()
 call(op='portrait_state',click='portraitFeatureSide_eyes_1')
 call(op='portrait_state',parameter='feature.eyeSize',value=20);wait()
 call(op='portrait_state',parameter='feature.smile',value=30);state=wait()
 params=state['recipe']['people'][0]['settings']['parameters']
 result={'eyeLeftThenSmile':params,'status':state['status']}
 call(op='portrait_state',capture=str(OUT/'left-eye-then-smile.png'))
 call(op='portrait_state',click='portraitFeatureSide_eyes_0');call(op='portrait_state',parameter='feature.eyeSize',value=0);wait()
 call(op='portrait_state',parameter='feature.smile',value=0);wait()
 checks=[]
 for panel,key,value in [(0,'face.width',60),(2,'head.size',30),(2,'face.forehead',30),(2,'head.hairline',30),(2,'head.crown',40),(2,'head.yaw',30)]:
  call(op='portrait_state',panel=panel,parameter=key,value=value);state=wait()
  checks.append({'key':key,'value':value,'status':state['status'],'previewBusy':state['previewBusy']})
 result['combined']=checks
 assert all(c['status'].startswith('已调整') for c in checks),checks
 combined=state['recipe']
 call(op='portrait_state',history=-1);wait();call(op='portrait_state',history=1);assert wait()['recipe']==combined
 saved=call(op='portrait_state',source=str(ROOT/'docs/test-evidence/M3/quality/chris-hadfield-source.png'),render=str(OUT/'combined-after-fix.png'),save=str(OUT/'combined-after-fix.huelith'),checkRegion=True)
 assert saved['roundtrip'] and saved['regionError']<2e-6,saved
 result['comboUndoRedo']=True;result['comboRoundtrip']=saved['roundtrip']
 call(op='portrait_state',capture=str(OUT/'combined-window-after-fix.png'))
 (OUT/'targeted.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps(result,ensure_ascii=True))
finally:call(op='portrait_close');call(op='shutdown')
