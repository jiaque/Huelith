"""C03 bounded algorithm group: reuse existing pixel fixtures, omit obsolete UI paths."""
from pathlib import Path
import subprocess,sys
cuts={'color':'recipe=real;s=', 'smooth':'recipe=real;s=', 'roots':'recipe=real;s=', 'strands':"source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=real",'gradient':"source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=real"}
for name,cut in cuts.items():
 path=Path(f'tools/portrait_hair_{name}_validate.py').resolve();text=path.read_text(encoding='utf-8').split(cut)[0]
 subprocess.run([sys.executable,'-c',f'__file__={str(path)!r}\n'+text+"\ncall(op='shutdown')"],check=True)
 print(name,'algorithm fixture passed',flush=True)
