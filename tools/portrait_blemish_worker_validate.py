"""Compare isolated repair analysis with native results and reject corrupt models."""
from pathlib import Path
import base64,io,hashlib
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
os.environ.setdefault('HUELITH_AI_WORKER_EXE',str(ROOT/'build/windows-release/Huelith.exe'))
OUT=ROOT/'docs/test-evidence/M3/blemish-model'
source=ROOT/'docs/test-evidence/M3/quality/acne-face-source.png'
person=json.loads(source.with_name('acne-face-recipe.json').read_text())['people'][0]
model=Path(os.environ.get('HUELITH_BLEMISH_TEST_MODEL',str(ROOT/'research/m3/blemish-model-evaluation/blemish_detection.onnx')))
call(op='init',settings=str(OUT/'worker-settings'))
records=[]
def pixels(value):
    if isinstance(value,dict):return {k:pixels(v) for k,v in value.items()}
    if isinstance(value,list):return [pixels(v) for v in value]
    if isinstance(value,str) and value.startswith('iVBOR'):
        image=Image.open(io.BytesIO(base64.b64decode(value)))
        return (image.mode,image.size,hashlib.sha256(image.tobytes()).hexdigest())
    return value
for path,evidence in [('',None),(str(model),str(OUT/'acne-face-native.png'))]:
    expected=call(op='portrait_geometry',repairAnalysis=True,source=str(source),person=person,**({'blemishEvidence':evidence} if evidence else {}))
    actual=call(op='portrait_geometry',repairWorker=True,source=str(source),person=person,model=path)
    assert actual.pop('workerIsolated') is True
    assert actual.pop('cpuFallback') is False
    # QCoreApplication and QApplication use different default PNG DPI metadata.
    # Compare decoded masks exactly, plus every non-image analysis field.
    assert pixels(actual)==pixels(expected),'worker changed analysis pixels or suggestions'
    records.append(dict(model=bool(path),sameResult=True,candidates=len(actual['blemishes'])))
bad=OUT/'worker-corrupt.onnx';bad.write_bytes(b'invalid model')
try:
    call(op='portrait_geometry',repairWorker=True,source=str(source),person=person,model=str(bad))
    raise AssertionError('corrupt model accepted')
except RuntimeError as error:
    assert '校验' in str(error),str(error)
    failure=str(error)
# A failed worker must not poison the next request.
recovery=call(op='portrait_geometry',repairWorker=True,source=str(source),person=person,model='')
assert recovery['workerIsolated'] and recovery['blemishes']
(OUT/'worker-results.json').write_text(json.dumps(dict(cases=records,corruptModelError=failure,recovery=True),ensure_ascii=False,indent=2),encoding='utf-8')
call(op='shutdown')
print('Isolated baseline/fusion equal native; corrupt model rejected; next request recovers.')
