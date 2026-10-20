from pathlib import Path
exec(compile(Path('tools/portrait_b01_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b02';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'));results=[]
try:
 for name in ['chris-hadfield','ellen-ochoa','glasses']:
  source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'));wait();call(op='portrait_state',workspaceTask=11,select=0);s=wait()
  for amount in [20,100,-100,30]:
   print(name,"auto",amount,flush=True)
   call(op='portrait_state',parameter='head.size',value=amount);s=wait();assert s['status'].startswith('已调整'),s['status'];assert s['recipe']['people'][0]['settings']['parameters']['head.size']==-amount
  call(op='portrait_state',capture=str(OUT/f'{name}-076.png'),previewImage=str(OUT/f'{name}-auto.png'))
  call(op='portrait_state',workspaceTask=3,panel=2,parameter='head.size',value=40);wait();call(op='portrait_state',workspaceTask=11);s=wait();assert s['recipe']['people'][0]['settings']['parameters']['head.size']==-40
  call(op='portrait_state',parameter='head.size',value=0);wait();call(op='portrait_state',click='portraitHeadCorrect');s=wait();manual=s['recipe']['people'][0]['settings']['headManual'];a=manual['outline'][0];b=[a[0],max(0,a[1]-.008)]
  call(op='portrait_state',stroke=[a,b,b]);s=wait();assert s['status']=='头部范围已更新',s['status'];assert s['recipe']['people'][0]['settings']['headManual']['outline'][0]!=a
  manual=s['recipe']['people'][0]['settings']['headManual'];anchor=manual['anchor'];target=[anchor[0]+.005,anchor[1]+.008]
  call(op='portrait_state',stroke=[anchor,target,target]);s=wait();assert s['status']=='头部范围已更新',s['status'];assert s['recipe']['people'][0]['settings']['headManual']['anchor']!=anchor
  if name=='chris-hadfield':
   import math
   manual=s['recipe']['people'][0]['settings']['headManual'];points=manual['outline'];center=[sum(p[i] for p in points)/len(points) for i in range(2)];p=points[2];w,h=Image.open(source).size;face=s['recipe']['people'][0]['face'];dx,dy=p[0]-center[0],p[1]-center[1];span=math.hypot(dx*w,dy*h);distance=face[2]*w*manual['transition'];outer=[p[0]+dx*distance/span,p[1]+dy*distance/span];target=[outer[0]+dx*.12,outer[1]+dy*.12]
   call(op='portrait_state',stroke=[outer,target,target]);s=wait();assert s['status']=='头部范围已更新',s['status'];assert s['recipe']['people'][0]['settings']['headManual']['transition']>.65
  for amount in [20,100,-100,35]:
   print(name,"manual",amount,flush=True)
   call(op='portrait_state',parameter='head.size',value=amount);s=wait();assert s['status'].startswith('已调整'),s['status']
  if name=='chris-hadfield':
   call(op='portrait_state',workspaceTask=3,panel=0,parameter='face.width',value=-15);s=wait();assert s['status'].startswith('已调整'),s['status']
   call(op='portrait_state',panel=1,parameter='feature.eyeSize',value=10);s=wait();assert s['status'].startswith('已调整'),s['status']
   call(op='portrait_state',workspaceTask=11);s=wait();assert s['recipe']['people'][0]['settings']['parameters']['head.size']==-35
   call(op='portrait_state',click='portraitHeadBackground');s=wait();assert not s['recipe']['people'][0]['settings']['protectBackground']
   call(op='portrait_state',click='portraitHeadBackground');s=wait();assert s['recipe']['people'][0]['settings']['protectBackground']
  call(op='portrait_state',headMode=0);wait();call(op='portrait_state',headMode=1);s=wait();assert s['recipe']['people'][0]['settings']['parameters']['head.size']==-35
  call(op='pump',waitMs=100);call(op='portrait_state',capture=str(OUT/f'{name}-077.png'),previewImage=str(OUT/f'{name}-manual.png'))
  combined=s['recipe'];call(op='portrait_state',history=-1);wait();call(op='portrait_state',history=1);assert wait()['recipe']==combined
  r=call(op='portrait_state',source=str(source),render=str(OUT/f'{name}-output.png'),save=str(OUT/f'{name}.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
  call(op='portrait_close');call(op='portrait_window',project=str(OUT/f'{name}.huelith'));assert wait()['recipe']==combined
  results.append({'name':name,'autoManualExtremes':True,'crossEntry':True,'outlineDrag':True,'anchorDrag':True,'transitionDrag':name=='chris-hadfield','roundtrip':True});call(op='portrait_close')
 # A valid face with unavailable automatic scalp segmentation remains editable.
 missing=json.loads(json.dumps(combined));settings=missing['people'][0]['settings'];settings['parameters']['head.size']=0;settings['headManualEnabled']=False;settings.pop('headManual',None)
 for key in ['head','hair','skin']:settings['automaticMasks'].pop(key,None)
 call(op='portrait_window',path=str(source),recipe=missing,models=str(ROOT/'build/windows-release/models'));wait();call(op='portrait_state',workspaceTask=11,parameter='head.size',value=30);s=wait();assert '自动头部范围不足' in s['status'];assert s['recipe']['people'][0]['settings']['parameters']['head.size']==0
 call(op='portrait_state',click='portraitHeadCorrect');wait();call(op='portrait_state',parameter='head.size',value=30);s=wait();assert s['status'].startswith('已调整'),s['status'];assert s['recipe']['people'][0]['settings']['parameters']['head.size']==-30
 call(op='portrait_state',doubleClickParameter='head.size');assert wait()['recipe']['people'][0]['settings']['parameters']['head.size']==0
 results.append({'automaticFailureToManual':True,'doubleClickReset':True})
 (OUT/'result.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print(json.dumps(results))
finally:call(op='portrait_close');call(op='shutdown')
