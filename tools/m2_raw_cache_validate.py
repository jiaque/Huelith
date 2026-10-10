"""Bounded, lossless RAW cache and input identity via the production C++ DLL."""
import ctypes
import json
import os
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    qt = Path(r'D:\Program Files\Qt\6.10.3\msvc2022_64')
    handles = [os.add_dll_directory(str(qt / 'bin')),
               os.add_dll_directory(str(ROOT / 'build/windows-release'))]
    os.environ['QT_QPA_PLATFORM'] = 'offscreen'
    os.environ['QT_PLUGIN_PATH'] = str(qt / 'plugins')
    lib = ctypes.CDLL(str(ROOT / 'build/windows-release/huelith_python_driver.dll'))
    fn = lib.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p

    def call(op='raw_cache', **values):
        result = json.loads(fn(json.dumps(dict(op=op, **values)).encode()))
        return result

    checks = []
    with tempfile.TemporaryDirectory(prefix='huelith-cache-') as folder:
        call('init', settings=folder)
        # HDR, negative scene-linear values and fractional alpha must survive disk spill.
        pixels = [-.25, .5, 4., .5] * 4
        call(memoryBudget=64, diskBudget=128)
        call(key='a', pixels=pixels)
        call(key='b', pixels=[.125, .25, .5, 1.] * 4)
        restored = call(key='a', get=True)
        assert restored['pixels'] == pixels and restored['colorSpacePreserved'], restored
        assert restored['memory'] <= 64 and restored['disk'] == 128
        checks.append('float_hdr_alpha_color_space_disk_spill_exact')
        call(key='c', pixels=pixels)
        assert call(key='a')['contains'] and not call(key='b')['contains']
        assert call(key='c')['disk'] == 128
        checks.append('disk_budget_evicts_least_recently_used')
        call(memoryBudget=0, diskBudget=64)
        call(key='a', pixels=pixels)
        restored = call(key='a', get=True)
        assert restored['pixels'] == pixels and restored['memory'] == 0
        checks.append('zero_memory_budget_roundtrip')
        call(memoryBudget=0, diskBudget=32)
        assert 'error' in call(key='large', pixels=pixels)
        assert not call(key='large')['contains']
        checks.append('oversize_rejected_without_partial_entry')
        model = Path(folder) / 'model.onnx'
        model.write_bytes(b'first')
        args = dict(source='raw-a', denoise=True, model=str(model))
        key = call('raw_cache_key', **args)['key']
        assert key == call('raw_cache_key', **args)['key']
        for override in (dict(source='raw-b'), dict(exposure=1), dict(directml=False), dict(denoise=False)):
            assert key != call('raw_cache_key', **(args | override))['key']
        model.write_bytes(b'other')  # Same path and size; content must invalidate.
        assert key != call('raw_cache_key', **args)['key']
        model.unlink()
        assert 'error' in call('raw_cache_key', **args)
        checks.append('source_model_content_backend_exposure_invalidation')
        call('shutdown')
    report = dict(passed=len(checks), total=len(checks), checks=checks)
    output = ROOT / 'docs/test-evidence/M2/raw/cache.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
