from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-c02';OUT.mkdir(exist_ok=True)
source=ROOT/'research/m3/portraits/jassi-standing.jpg';recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text())
for p in recipe['people']:p['settings']['parameters']={}
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=8,select=0);wait()
 for group in range(6):
  if group:call(op='portrait_state',click=f'portraitBodyGroup{group}');wait()
  call(op='portrait_state',capture=str(OUT/f'{51+group:03}.png'))
 call(op='portrait_state',bodyMode=1);wait()
 for tool,key in enumerate(['body.manualHeight','body.manualSlim','body.manualScale','body.manualRotate']):
  call(op='portrait_state',bodyTool=tool);wait();call(op='portrait_state',parameter=key,value=25);s=wait()
  assert s['recipe']['people'][0]['settings']['parameters'].get(key)==25,(key,s['status'])
  call(op='portrait_state',capture=str(OUT/f'{57+tool:03}.png'))
 saved=s['recipe'];call(op='portrait_state',workspaceTask=3);wait();call(op='portrait_state',workspaceTask=8);assert wait()['recipe']==saved
 call(op='portrait_state',bodyMode=2,click='portraitBodyMaterial_collarbone');s=wait();assert len(s['recipe']['people'][0]['settings']['bodyLinesQuad'])==4
 call(op='portrait_state',capture=str(OUT/'061.png'))
 for i,name in [(1,'062'),(2,'063')]:
  call(op='portrait_state',click=f'portraitBodyLineAction{i}');wait();call(op='portrait_state',capture=str(OUT/f'{name}.png'))
 # Move then fit the actual canvas quad; erase is retained in source-person coordinates.
 call(op='portrait_state',click='portraitBodyLineAction0');s=wait();quad=np.array(s['recipe']['people'][0]['settings']['bodyLinesQuad']);center=quad.mean(0)
 r=call(op='portrait_geometry',size=list(Image.open(source).size),recipe=s['recipe'],field=str(OUT/'field.bin'));w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
 start=forward_points(f,center[None,:])[0];call(op='portrait_state',paintPath=[start.tolist(),(start+[.01,.02]).tolist()]);s=wait();moved=np.array(s['recipe']['people'][0]['settings']['bodyLinesQuad']);assert np.linalg.norm(moved-quad)>.01
 call(op='portrait_state',click='portraitBodyLineAction1');wait();start=forward_points(f,moved[:1])[0];call(op='portrait_state',paintPath=[start.tolist(),(start+[-.01,.005]).tolist()]);s=wait();assert s['recipe']['people'][0]['settings']['bodyLinesQuad']!=moved.tolist()
 call(op='portrait_state',click='portraitBodyLineAction2',brushSize=70,paintPath=[start.tolist(),(start+[.02,.01]).tolist()]);s=wait();assert 'contour.lines' in s['recipe']['people'][0]['settings']['masks']
 before=wait()['recipe'];call(op='portrait_state',click='portraitBodyLineAction3');deleted=wait();assert deleted['recipe']['people'][0]['settings']['parameters'].get('contour.lines',0)==0
 call(op='portrait_state',history=-1);assert wait()['recipe']==before,'template deletion undo lost metadata'
 r=call(op='portrait_state',source=str(source),save=str(OUT/'body.huelith'),render=str(OUT/'saved.png'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'body.huelith'));assert wait()['recipe']==saved
 print('C02 pages, manual transforms, cross-entry preservation, template deletion undo and save passed')
finally:call(op='portrait_close');call(op='shutdown')
