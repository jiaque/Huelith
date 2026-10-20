"""Validate the delivered executable without developer PATH/Qt overrides."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--directory', type=Path, default=ROOT/'build/windows-release')
args = parser.parse_args()
directory = args.directory.resolve()
required = ['Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Svg.dll',
            'platforms/qwindows.dll', 'imageformats/qjpeg.dll',
            'iconengines/qsvgicon.dll', 'msvcp140.dll', 'vcruntime140.dll']
missing = [name for name in required if not (directory/name).is_file()]
if missing:
    raise RuntimeError('Incomplete deployment: '+', '.join(missing))
environment = {k:v for k,v in os.environ.items()
               if not k.upper().startswith(('QT_', 'QML_', 'PYTHON', 'HUELITH_'))}
environment['PATH'] = str(Path(environment.get('SystemRoot', 'C:/Windows'))/'System32')
# Suppress OS loader dialogs in this child-only validation process.
ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
with tempfile.TemporaryDirectory(prefix='huelith-startup-', dir=ROOT/'build',
                                 ignore_cleanup_errors=True) as working:
    result = subprocess.run([str(directory/'Huelith.exe'), '--smoke-test'],
                            cwd=working, env=environment, capture_output=True,
                            text=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
report = dict(directory=str(directory), exitCode=result.returncode,
              path=environment['PATH'], externalWorkingDirectory=True,
              localDependencies=required, stderr=result.stderr)
evidence = ROOT/'docs/test-evidence/M3/windows-startup'
evidence.mkdir(parents=True, exist_ok=True)
(evidence/'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=True))
result.check_returncode()
