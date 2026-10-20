"""Focused visual check of approved navigation icons; no renderer regression."""
import os,json,tempfile
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/workspace-a01/navigation'
O.mkdir(parents=True,exist_ok=True)
source=R/'docs/test-evidence/M3/quality/portrait-highres-source.png'
recipe=json.loads((source.parent/'portrait-highres-recipe.json').read_text())
with tempfile.TemporaryDirectory(dir=R/'build',ignore_cleanup_errors=True) as tmp:
 checked(op='init',settings=tmp)
 try:
  checked(op='portrait_window',path=str(source),recipe=recipe)
  settle()
  checked(op='portrait_state',workspaceTask=3,resize=[1600,1000])
  checked(op='pump',waitMs=100)
  state=checked(op='portrait_state',workspaceLayout=True,capture=str(O/'navigation-1600.png'))
  assert len(state['workspaceTasks'])==15
  checked(op='portrait_state',resize=[1280,1000])
  checked(op='pump',waitMs=100)
  checked(op='portrait_state',capture=str(O/'navigation-1280.png'))
 finally:
  checked(op='portrait_close');checked(op='shutdown')
print('Navigation captures ready: 15 entries, 1600 and 1280 logical widths')
