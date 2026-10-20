from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b04';OUT.mkdir(exist_ok=True)
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-forehead-volume/real-recipe.json').read_text(encoding='utf-8'))
recipe['people'][0]['settings']['parameters']={'feature.noseWidth':12}
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=0,select=0);initial=wait();records=[]
 for mode,count in [(0,7),(1,6)]:
  call(op='portrait_state',volumeMode=mode);wait()
  for part in range(1,count):
   call(op='portrait_state',click='portraitVolumeCard'+str(part),portraitVolumeAmount=60);state=wait();params=state['recipe']['people'][0]['settings']['parameters'];assert params['feature.noseWidth']==12
   assert state['history']>initial['history'],state
   out=OUT/f'part-{mode}-{part}.png';call(op='portrait_state',previewImage=str(out))
   records.append({'mode':mode,'part':part,'parameters':params,'status':state['status']})
   call(op='portrait_state',portraitVolumeAmount=0);wait()
  call(op='portrait_state',click='portraitVolumeCard0',portraitVolumeAmount=100);state=wait();mix=state['recipe']['people'][0]['settings']['liftMix' if mode else 'volumeMix'];assert mix['amount']==100
  call(op='portrait_state',portraitVolumeAmount=50);half=wait();params=half['recipe']['people'][0]['settings']['parameters']
  for key,value in mix['target'].items():assert abs(params[key]-(mix['base'][key]+value*.5))<1e-6,(key,params)
  call(op='portrait_state',capture=str(OUT/('002.png' if mode else '001.png')))
  call(op='portrait_state',portraitVolumeAmount=0);zero=wait()
  for key,value in mix['base'].items():assert abs(zero['recipe']['people'][0]['settings']['parameters'][key]-value)<1e-6
  call(op='portrait_state',history=-1);assert wait()['recipe']==half['recipe'];call(op='portrait_state',history=1);assert wait()['recipe']==zero['recipe']
 call(op='portrait_state',volumeMode=0,click='portraitVolumeCard2',portraitVolumeAmount=100);state=wait();params=state['recipe']['people'][0]['settings']['parameters'];assert params['face.templeFill.left']==100 and params['face.templeFill.right']==100
 temple=field('temple');assert np.max(np.abs(temple))>1e-5
 import copy,base64,io
 black=Image.new('L',(64,64));buffer=io.BytesIO();black.save(buffer,format='PNG')
 masked=copy.deepcopy(state['recipe']);masked['people'][0]['settings'].setdefault('masks',{})['face.templeFill']=base64.b64encode(buffer.getvalue()).decode()
 zero=copy.deepcopy(state['recipe']);zero['people'][0]['settings']['parameters']['face.templeFill.left']=0;zero['people'][0]['settings']['parameters']['face.templeFill.right']=0
 fields=[]
 for name,r in [('masked',masked),('zero',zero)]:
  result=call(op='portrait_geometry',size=size.tolist(),recipe=r,field=str(OUT/(name+'.bin')));w,h=result['meshSize'];fields.append(np.fromfile(OUT/(name+'.bin'),np.float32).reshape(h,w,4)[:,:,:2])
 assert np.max(np.abs(fields[0]-fields[1]))<1e-6
 change=temple-fields[1];landmarks=np.array(recipe['people'][0]['landmarks'])[:,:2]
 protected=sample(change,landmarks[list(range(17,27))+list(range(36,66))])*(size-1)
 assert np.max(np.linalg.norm(protected,axis=1))<.8,np.max(np.linalg.norm(protected,axis=1))
 call(op='portrait_state',workspaceTask=3,panel=0,parameter='face.width',value=-20);wait();call(op='portrait_state',workspaceTask=0);wait();combined=field('temple-width')
 result=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'combined.huelith'),checkRegion=True);assert result['roundtrip'] and result['regionError']<2e-6
 saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'combined.huelith'));assert wait()['recipe']==saved
 (OUT/'result.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8');print('B04 group passed')
finally:call(op='portrait_close');call(op='shutdown')
