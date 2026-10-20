"""B01 orientation/face composition and approved-page captures, offscreen."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
os.environ['TEMP']=os.environ['TMP']=str(ROOT/'build/portrait-a01-temp')
os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe')
OUT=ROOT/'docs/test-evidence/M3/workspace-b01';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png'
def wait():
 end=time.monotonic()+90
 while time.monotonic()<end:
  call(op='pump',waitMs=30);s=call(op='portrait_state')
  if s['ready'] and not s['previewBusy']:return s
 raise AssertionError(s['status'])
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'));wait()
 call(op='portrait_state',workspaceTask=3,select=0,resize=[1600,1000]);wait()
 for panel,number in [(0,'033'),(1,'034'),(2,'035')]:
  call(op='portrait_state',panel=panel);wait()
  call(op='portrait_state',capture=str(OUT/(number+'.png')))
 for panel,key,value in [(0,'face.width',-20),(1,'feature.eyeSize',15),(2,'head.yaw',40),(0,'face.chin',10),(2,'head.pitch',-35)]:
  call(op='portrait_state',panel=panel,parameter=key,value=value);state=wait()
  values=state['recipe']['people'][0]['settings']['parameters']
  if key=='feature.eyeSize':assert values[key+'.left']==values[key+'.right']==value
  else:assert values[key]==value,(key,state['status'])
 combined=state['recipe']
 call(op='portrait_state',history=-1);wait();call(op='portrait_state',history=1)
 assert wait()['recipe']==combined
 saved=call(op='portrait_state',source=str(source),render=str(OUT/'combined.png'),save=str(OUT/'combined.huelith'),checkRegion=True)
 assert saved['roundtrip'] and saved['regionError']<2e-6,saved
 call(op='portrait_state',doubleClickParameter='head.pitch');assert wait()['recipe']['people'][0]['settings']['parameters']['head.pitch']==0
 call(op='portrait_close');call(op='portrait_window',project=str(OUT/'combined.huelith'));reopened=wait()
 assert reopened['recipe']==combined
 old=json.loads(json.dumps(combined));old['people'][0]['settings'].pop('faceSurface')
 for key in ['head.yaw','head.pitch']:old['people'][0]['settings']['parameters'][key]=0
 call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=old,models=str(ROOT/'build/windows-release/models'));wait()
 call(op='portrait_state',workspaceTask=3,panel=2,parameter='head.yaw',value=25);backfilled=wait()
 values=backfilled['recipe']['people'][0]['settings']['parameters']
 assert values['head.yaw']==25 and values['face.width']==-20 and values['feature.eyeSize.left']==15,(values,backfilled['status'])
 assert len(backfilled['recipe']['people'])==1
 call(op='portrait_state',panel=0);before=wait()['recipe']
 call(op='portrait_state',hold='portraitGroupEnabled_0_轮廓',pressed=True);wait()
 assert call(op='portrait_state')['recipe']==before
 call(op='portrait_state',hold='portraitGroupEnabled_0_轮廓',pressed=False);wait()
 call(op='portrait_state',click='portraitGroupReset_0_轮廓');reset=wait()['recipe']
 assert reset['people'][0]['settings']['parameters'].get('face.width',0)==0
 assert reset['people'][0]['settings']['parameters']['head.yaw']==25
 call(op='portrait_state',history=-1);assert wait()['recipe']==before
 # Two independently photographed people in one canvas exercise target isolation.
 pair=Image.new('RGB',(1200,800));
 for i,name in enumerate(['leroy-chiao','chris-hadfield']):
  im=Image.open(ROOT/f'docs/test-evidence/M3/quality/{name}-source.png');im.thumbnail((600,800));pair.paste(im,(600*i,0))
 pairPath=OUT/'two-people-source.png';pair.save(pairPath)
 call(op='portrait_close');call(op='portrait_window',path=str(pairPath),models=str(ROOT/'build/windows-release/models'));state=wait()
 assert state['count']==2,state['status']
 call(op='portrait_state',workspaceTask=3,select=0,panel=0,parameter='face.width',value=-25);state=wait()
 second=state['recipe']['people'][1]['settings']
 call(op='portrait_state',select=1,panel=1,parameter='feature.noseWidth',value=20);state=wait()
 assert state['recipe']['people'][0]['settings']['parameters']['face.width']==-25
 assert state['recipe']['people'][1]['settings']['parameters']['feature.noseWidth']==20
 call(op='portrait_state',select=0,panel=2,parameter='head.yaw',value=20);state=wait()
 assert state['recipe']['people'][1]['settings']['parameters']['feature.noseWidth']==20
 call(op='portrait_state',capture=str(OUT/'two-people.png'))
 (OUT/'workflow-result.json').write_text(json.dumps({'roundtrip':True,'regionError':saved['regionError'],'mixedEditing':True,'undoRedo':True,'reopen':True},indent=2))
 print('B01 face + features + orientation composition, undo, reset, save and reopen passed')
finally:
 call(op='portrait_close');call(op='shutdown')
