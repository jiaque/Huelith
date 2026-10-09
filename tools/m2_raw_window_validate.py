"""Drive the production RAW dialog, including asynchronous cancellation and source persistence."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    qt = Path(r'D:\Program Files\Qt\6.10.3\msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows'
    lib = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = lib.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p
    def call(op, **values):
        result = json.loads(fn(json.dumps(dict(op=op, **values)).encode()))
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result
    def until(predicate):
        limit = time.monotonic() + 90
        while time.monotonic() < limit:
            call('pump', waitMs=30)
            state = call('raw_window_state')
            if predicate(state):
                return state
        raise TimeoutError(state)
    output = ROOT/'docs/test-evidence/M2/raw'
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='huelith-raw-window-') as folder:
        call('init', settings=folder)
        call('window')
        source = dict(path=str(ROOT/'research/m0/samples/r10-5871.CR3'),
                      model=str(ROOT/'research/m0/model-package/rawdenoise-nind/model_bayer.onnx'),
                      denoise=True)
        call('raw_window', **source)
        call('pump', waitMs=30)
        call('raw_window_state', reject=True)
        cancelled = until(lambda state: not state['visible'])
        assert cancelled['result'] == 0
        print('Cancelled active RAW session', flush=True)
        call('raw_window', **source)
        ready = until(lambda state: state['ready'])
        call('raw_window_state', exposure=.5)
        call('raw_window_state', capture=str(output/'raw-window.png'))
        accepted = call('raw_window_state', accept=True)
        assert accepted['result'] == 1
        call('raw_window_state', save=str(Path(folder)/'raw.huelith'))
        print('RAW source, recipe and float pixels round-tripped', flush=True)
        call('shutdown')
    (output/'window.json').write_text(json.dumps(dict(cancelled=cancelled, ready=ready,
        accepted=accepted, source_recipe_pixels_roundtrip=True), ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
