"""Check sample hashes, recorded production results and current capability detection."""
import ctypes, hashlib, json, os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/multibrand'
handles=[os.add_dll_directory(str(ROOT/'build/windows-release')),os.add_dll_directory('D:/Program Files/Qt/6.10.3/msvc2022_64/bin')]
lib=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
fn=lib.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
rows=[]
for sample in json.loads((ROOT/'research/m2/multibrand/manifest.json').read_text(encoding='utf-8')):
    path=Path(sample['path']);record=json.loads((OUT/(sample['id']+'.json')).read_text(encoding='utf-8'))
    assert hashlib.sha256(path.read_bytes()).hexdigest()==sample['sha256']==record['sha256']
    cap=json.loads(fn(json.dumps(dict(op='raw_capabilities',path=str(path))).encode()))
    assert cap['recognized'] and record['plain']['finite'] and 'error' not in record['plain']
    for mode,field in [('ai','rawDenoiseSupported'),('dng','rawDngSupported')]:
        assert ('error' not in record[mode])==cap[field],(path,mode)
    if cap['rawDngSupported']:
        d=record['dng'];assert d['originalUnchanged'] and d['sameSize'] and d['before']['sensorSha256']==d['after']['sensorSha256'] and d['maxError']<1e-4
    rows.append(dict(file=path.name,camera=sample['make']+' '+sample['model'],capabilities=cap,ordinary=True,ai=cap['rawDenoiseSupported'],dng=cap['rawDngSupported'],passed=True))
(OUT/'matrix.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS:',len(rows),'real samples;',sum(r['ai'] for r in rows),'AI;',sum(r['dng'] for r in rows),'sensor DNG')
