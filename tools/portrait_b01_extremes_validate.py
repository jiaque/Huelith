"""One bounded B01 UI sweep; record failures rather than count tests as acceptance."""
from pathlib import Path
import re
exec(compile(Path('tools/portrait_b01_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b01/extremes';OUT.mkdir(parents=True,exist_ok=True)
text=(ROOT/'src/editing/portrait_effects.cpp').read_text(encoding='utf-8')
keys={panel:re.findall(r'\{"([a-zA-Z.]+)"',text.split(f'case {panel}:return ')[1].split(';')[0]) for panel in [0,1,2]}
keys[0]=[k for k in keys[0] if k in ['face.width','face.small','face.vShape','face.cheek','face.jawAngle','face.smoothContour','face.chin','face.chinWidth','face.doubleChin']]
keys[2]=[k for k in keys[2] if k!='head.roll']+['face.forehead']
call(op='init',settings=str(OUT/'settings'));records=[]
try:
 call(op='portrait_window',path=str(ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png'),models=str(ROOT/'build/windows-release/models'));wait()
 call(op='portrait_state',workspaceTask=3,select=0);wait()
 for panel,controls in keys.items():
  call(op='portrait_state',panel=panel);wait()
  for key in controls:
   for value in ([-100,100] if key not in ['face.small','face.vShape','face.smoothContour','face.doubleChin'] else [100]):
    call(op='portrait_state',parameter=key,value=value);s=wait()
    records.append({'key':key,'value':value,'status':s['status'],'ok':s['status'].startswith('已调整')})
    call(op='portrait_state',doubleClickParameter=key);wait()
  print('finished',panel,flush=True)
 (OUT/'result.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps([r for r in records if not r['ok']],ensure_ascii=True))
finally:call(op='portrait_close');call(op='shutdown')
