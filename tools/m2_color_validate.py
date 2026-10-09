"""Float tone and TIFF preservation tests through the production DLL."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import tempfile
import traceback
import math

ROOT = Path(__file__).resolve().parents[1]


def main():
    qt = Path(r'D:\Program Files\Qt\6.10.3\msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_QPA_PLATFORM'] = 'offscreen'
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    dll = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = dll.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p
    def call(op, **values):
        return json.loads(fn(json.dumps(dict(op=op, **values)).encode()))
    results = []
    def test(name, body):
        try:
            body()
            results.append(dict(name=name, passed=True))
        except Exception:
            results.append(dict(name=name, passed=False, error=traceback.format_exc()))
        print(results[-1], flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-color-') as directory:
        call('init', settings=directory)
        def tone(settings, rgba=(.125, .25, .5, .5), **extra):
            result = call('tone_probe', settings=dict(version=1, **settings), rgba=rgba, **extra)
            if 'error' in result:
                raise RuntimeError(result['error'])
            return result['rgba']
        def identity():
            assert tone({}) == [.125, .25, .5, .5]
        test('identity_exact_premultiplied_alpha', identity)
        def exposure():
            result = tone(dict(exposure=1), rgba=(.5, 1, 2, .5))
            assert max(abs(a-b) for a,b in zip(result,(1,2,4,.5))) < .001, result
        test('exposure_hdr_no_sdr_clamp_alpha_unchanged', exposure)
        def curve():
            value = tone(dict(curve=[[0,0],[.5,.7],[1,1]]), rgba=(.18,.18,.18,1))
            assert min(value[:3]) > .18 and max(value[:3])-min(value[:3]) < .001
            bad = call('tone_probe', settings=dict(version=1, curve=[[0,0],[.5,.5],[.5,.7],[1,1]]), rgba=[0,0,0,1])
            assert 'error' in bad
        test('curve_brightens_neutral_reject_duplicate_nodes', curve)
        def channels():
            for field in ('contrast','highlights','shadows','whites','blacks','temperature','tint','saturation','vibrance'):
                value = tone({field:25})
                assert all(map(math.isfinite,value)) and value[3] == .5, (field, value)
            value = tone(dict(blackPoint=.1,whitePoint=.9,gamma=1.2))
            assert all(map(math.isfinite,value))
        test('tone_parameters_finite_alpha_preserved', channels)
        def persistence():
            tone(dict(exposure=.3,curve=[[0,.02],[.5,.7],[1,.98]],hue=[10,0,0,0,0,0,0,0]), save=str(Path(directory)/'tone.huelith'))
        test('tone_recipe_and_render_roundtrip', persistence)
        def tiff():
            path = Path(directory)/'16位 AdobeRGB.tif'
            result = call('tiff_roundtrip',path=str(path))
            assert 'error' not in result, result
            assert result['maximumError'] < 1e-6, result
            original = hashlib.sha256(path.read_bytes()).hexdigest()
            cancelled = call('tiff_roundtrip',path=str(path),cancelled=True)
            assert '取消' in cancelled['error']
            assert hashlib.sha256(path.read_bytes()).hexdigest() == original
        test('tiff_16bit_icc_alpha_unicode_roundtrip_cancel_atomic', tiff)
        call('shutdown')
    report = dict(tests=results, passed=sum(x['passed'] for x in results), total=len(results))
    destination = ROOT/'docs/test-evidence/M2/color'
    destination.mkdir(parents=True,exist_ok=True)
    (destination/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    assert report['passed'] == report['total']


if __name__ == '__main__':
    main()
