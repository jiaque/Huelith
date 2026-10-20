"""Check a cropped oblique face through the production B01 workflow."""
from pathlib import Path
exec(compile(Path('tools/portrait_b01_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],str(Path('tools/portrait_b01_workflow_validate.py').resolve()),'exec'))
source=ROOT/'docs/test-evidence/M3/quality/acne-profile-source.png'
call(op='init',settings=str(OUT/'profile-settings'))
try:
 call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'));s=wait()
 call(op='portrait_state',workspaceTask=3,select=0,panel=0);s=wait()
 if s['recipe']['people'][0]['settings'].get('imageScope'):
  assert '人脸' in s['status']
 else:
  call(op='portrait_state',parameter='face.width',value=-15);s=wait()
 call(op='portrait_state',source=str(source),render=str(OUT/'profile-result.png'),capture=str(OUT/'profile-window.png'))
 (OUT/'profile-result.json').write_text(json.dumps(s,ensure_ascii=False),encoding='utf-8')
 print('Profile recorded; inspect visible-face handling and output')
finally:call(op='portrait_close');call(op='shutdown')
