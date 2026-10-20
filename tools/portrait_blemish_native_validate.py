"""Compare native ONNX evidence with the original detection head, without UI activation."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/blemish-model';model=ROOT/'research/m3/blemish-model-evaluation/blemish_detection.onnx'
call(op='init',settings=str(OUT/'native-settings'));records=[]
for name in ['acne-face','jessica-watkins','john-glenn']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';person=json.loads((source.parent/f'{name}-recipe.json').read_text())['people'][0]
    target=OUT/f'{name}-native.png';start=time.monotonic()
    call(op='portrait_geometry',source=str(source),person=person,blemishModel=str(model),mask=str(target))
    actual=np.asarray(Image.open(target)).astype(float)/255;expected=np.load(OUT/f'{name}-probability.npy')
    error=np.abs(actual-expected);records.append(dict(name=name,seconds=time.monotonic()-start,maximumError=float(error.max()),meanError=float(error.mean())))
    print(records[-1],flush=True)
bad=OUT/'invalid-model.bin';bad.write_bytes(b'not a model')
try:call(op='portrait_geometry',source=str(source),person=person,blemishModel=str(bad),mask=str(OUT/'invalid.png'))
except RuntimeError as e:assert '校验失败' in str(e),str(e)
else:raise AssertionError('corrupt model accepted')
call(op='shutdown');(OUT/'native-results.json').write_text(json.dumps(dict(runs=records,corruptModelRejected=True),indent=2))
assert all(r['maximumError']<.01 for r in records),records
