"""One bounded B01 UI sweep; record failures rather than count tests as acceptance."""
from pathlib import Path
import re
exec(compile(Path('tools/portrait_b01_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b01/audit';OUT.mkdir(parents=True,exist_ok=True)
text=(ROOT/'src/editing/portrait_effects.cpp').read_text(encoding='utf-8')
keys={panel:re.findall(r'\{"([a-zA-Z.]+)"',text.split(f'case {panel}:return ')[1].split(';')[0]) for panel in [0,1,2]}
keys[0]=[k for k in keys[0] if k in ['face.width','face.small','face.vShape','face.cheek','face.jawAngle','face.smoothContour','face.chin','face.chinWidth','face.doubleChin']]
keys[2]=[k for k in keys[2] if k!='head.roll']+['face.forehead']
call(op='init',settings=str(OUT/'settings'));records=[]
try:
 call(op='portrait_window',path=str(ROOT/'docs/test-evidence/M3/quality/chris-hadfield-source.png'),models=str(ROOT/'build/windows-release/models'));state=wait()
 call(op='portrait_state',workspaceTask=3,select=0);wait()
 for panel,controls in keys.items():
  call(op='portrait_state',panel=panel);wait()
  for key in controls:
   try:
    call(op='portrait_state',parameter=key,value=30);state=wait()
    person=state['recipe']['people'][state['selected']];params=person['settings'].get('parameters',{})
    actual={k:v for k,v in params.items() if k==key or k.startswith(key+'.')}
    record={'panel':panel,'key':key,'requested':30,'actual':actual,'status':state['status']}
    state=call(op='portrait_state',previewField=str(OUT/'field.bin'))
    mw,mh=state['previewMeshSize']
    if mw and mh:
     f=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2];record['maxDisplacement']=float(np.max(np.abs(f)))
    records.append(record)
    if key in ['head.size','face.doubleChin','head.crown','head.hairline','head.hairVolume']:
     call(op='portrait_state',capture=str(OUT/(key+'.png')))
    call(op='portrait_state',doubleClickParameter=key);wait()
   except Exception as e:records.append({'key':key,'error':str(e)});break
  print('checked panel',panel,len(controls),flush=True)
 call(op='portrait_state',click='portraitDetect');state=wait()
 records.append({'redetectionCount':state['count'],'status':state['status']})
 (OUT/'result.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps(records,ensure_ascii=True))
finally:call(op='portrait_close');call(op='shutdown')
