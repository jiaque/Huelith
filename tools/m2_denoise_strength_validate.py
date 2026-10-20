"""Denoise endpoints, bounded cache and real RAW amount interaction, production DLL only."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import time
import traceback
import sys
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'docs/test-evidence/M2/denoise-strength'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    dll = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = dll.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p
    def raw(op, **values):
        return json.loads(fn(json.dumps(dict(op=op, **values)).encode()))
    def call(op, **values):
        r = raw(op, **values)
        assert 'error' not in r, r
        return r
    records = []
    def test(name, body):
        try:
            body()
            records.append(dict(name=name, passed=True))
        except Exception:
            records.append(dict(name=name, passed=False, error=traceback.format_exc()))
        print(records[-1], flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-denoise-') as folder:
        call('init', settings=folder)
        def endpoints():
            before = [-.25, .5, 4, .5]*4
            after = [.125, .25, 2, .5]*4
            call('raw_cache', memoryBudget=0, diskBudget=256)
            call('raw_cache', key='pair', pixels=after, reference=before)
            for amount in range(101):
                r = call('raw_cache', key='pair', get=True, amount=amount)
                expected = np.array(before)+(np.array(after)-before)*(amount/100)
                assert np.allclose(r['pixels'], expected, atol=3e-7, rtol=0), (amount, r)
                assert r['reference'] == before and r['memory'] == 0 and r['disk'] == 128, r
            for amount in [-1, 101]:
                assert 'error' in raw('raw_cache', key='pair', get=True, amount=amount)
            call('raw_cache', key='other', pixels=before, reference=after)
            call('raw_cache', key='third', pixels=before, reference=after)
            assert not call('raw_cache', key='pair')['contains']
            assert call('raw_cache', key='third')['disk'] == 256
        test('101_strengths_numpy_hdr_alpha_exact_endpoint_and_paired_disk_lru', endpoints)
        def ui():
            call('window')
            call('raw_window', path=str(ROOT/'research/m0/samples/r10-5871.CR3'), denoise=True)
            def wait():
                deadline = time.monotonic()+240
                s = {}
                while time.monotonic() < deadline:
                    call('pump', waitMs=30)
                    s = call('raw_window_state')
                    if s['idle'] and s['ready'] and s['previewPresented'] == s['previewRequested'] and s['previewPresented']:
                        return s
                raise TimeoutError(s)
            s = wait()
            assert s['recipe']['metadata']['denoise'], s
            call('raw_window_state', section=7, control='rawDenoiseAmount', value=0)
            wait()
            call('raw_window_state', capture=str(OUT/'amount-0.png'))
            call('raw_window_state',previewOutput=str(OUT/'endpoint0-'))
            latencies = []
            for amount in range(0, 101, 2):
                start = time.perf_counter()
                s = call('raw_window_state', control='rawDenoiseAmount', value=amount)
                latencies.append((time.perf_counter()-start)*1000)
                assert s['idle'] and s['ready'], s
                call('pump', waitMs=12)
            s = wait()
            assert s['recipe']['denoiseAmount'] == 100
            call('raw_window_state', capture=str(OUT/'amount-100.png'))
            call('raw_window_state',previewOutput=str(OUT/'endpoint100-'))
            original_recipe=s['recipe']
            call('raw_window_state',widget='rawDenoiseCompare',position=[.5,.5],mousePhase='press')
            held=wait();assert held['recipe']==original_recipe
            call('raw_window_state',previewOutput=str(OUT/'held-'))
            call('raw_window_state',widget='rawDenoiseCompare',position=[.5,.5],mousePhase='release')
            released=wait();assert released['recipe']==original_recipe
            call('raw_window_state',previewOutput=str(OUT/'released-'))
            images={k:np.array(Image.open(OUT/(k+'-rawPreview.png'))) for k in ['endpoint0','endpoint100','held','released']}
            assert np.array_equal(images['held'],images['endpoint0'])
            assert np.array_equal(images['released'],images['endpoint100'])
            assert not np.array_equal(images['endpoint0'],images['endpoint100'])
            call('pump', waitMs=400)
            call('raw_window_state', control='rawDenoiseAmount', value=37)
            wait()
            call('pump', waitMs=400)
            call('raw_window_state', click='rawUndo')
            assert wait()['recipe']['denoiseAmount'] == 100
            call('raw_window_state', click='rawRedo')
            assert wait()['recipe']['denoiseAmount'] == 37
            call('raw_window_state', accept=True)
            call('raw_window_state', save=str(Path(folder)/'denoise.huelith'))
            (OUT/'latency.json').write_text(json.dumps(dict(maximum_event_ms=max(latencies), events=len(latencies), model_recomputed=False), indent=2), encoding='utf-8')
        if '--failures-only' not in sys.argv:
            test('real_model_cached_strength_live_preview_undo_redo_source_save', ui)
        def failures():
            call('window')
            call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),denoise=False)
            def settle():
                deadline=time.monotonic()+180
                while time.monotonic()<deadline:
                    call('pump',waitMs=30);s=call('raw_window_state')
                    if s['idle']:return s
                raise TimeoutError(s)
            settle()
            missing=Path(folder)/'missing.onnx'
            call('raw_window_state',modelChoice=1,denoise=True,model=str(missing))
            call('raw_window_state',click='rawCompute');s=settle()
            assert not s['ready'] and '缺失' in s['status'],s
            invalid=Path(folder)/'invalid.onnx';invalid.write_bytes(b'invalid model')
            call('raw_window_state',model=str(invalid))
            call('raw_window_state',click='rawCompute');s=settle()
            assert not s['ready'] and s['visible'],s
            call('raw_window_state',modelChoice=0)
            call('raw_window_state',click='rawCompute');s=settle()
            assert s['ready'] and s['recipe']['metadata']['denoise'],s
            # Cancel an actual active inference and reopen in the same process.
            call('raw_window_state',reject=True)
            call('raw_window',path=str(ROOT/'research/m0/samples/r10-5873.CR3'),denoise=True)
            call('raw_window_state',reject=True)
            deadline=time.monotonic()+60
            while time.monotonic()<deadline:
                call('pump',waitMs=30);s=call('raw_window_state')
                if not s['visible']:break
            assert not s['visible'] and s['result']==0,s
            call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),denoise=False)
            s=settle();assert s['ready'],s
            call('raw_window_state',reject=True)
        test('missing_corrupt_model_recovery_active_cancel_and_reopen',failures)
        call('shutdown')
    report = dict(passed=sum(r['passed'] for r in records), total=len(records), tests=records)
    (OUT/('failure-results.json' if '--failures-only' in sys.argv else 'results.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    raise SystemExit(report['passed'] != report['total'])


if __name__ == '__main__':
    main()
