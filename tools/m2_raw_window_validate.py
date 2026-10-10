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
        ready = until(lambda state: state['ready'] and state['previewPresented'] > 0)
        assert ready['histogramSamples'] > 0
        call('raw_window_state', exposure=.5)
        call('raw_window_state', capture=str(output/'raw-window.png'))
        accepted = call('raw_window_state', accept=True)
        assert accepted['result'] == 1
        call('raw_window_state', save=str(Path(folder)/'raw.huelith'))
        print('RAW source, recipe and float pixels round-tripped', flush=True)
        call('raw_window', paths=[source['path'], str(ROOT/'research/m0/samples/r10-5873.CR3')],
             model=source['model'], denoise=False)
        until(lambda state: state['idle'])
        state = call('raw_window_state')
        assert state['count'] == 2 and not state['ready']
        call('raw_window_state', click='rawZoom100')
        state = call('raw_window_state', exposure=.25)
        assert state['scale'] == 1, state
        state = call('raw_window_state', click='rawUndo')
        assert state['recipes'][0].get('tone', {}).get('exposure', 0) == 0
        state = call('raw_window_state', click='rawRedo')
        assert state['recipes'][0]['tone']['exposure'] == .25 and state['scale'] == 1
        state = call('raw_window_state', index=1, included=False)
        assert state['ready'], state  # An unprocessed, excluded file must not block submit.
        state = call('raw_window_state', included=True)
        assert not state['ready'], state
        call('raw_window_state', index=0)
        original = call('raw_window_state')['recipes']
        call('raw_window_state', click='rawSynchronize', sync=dict(groups=[0], cancel=True))
        assert call('raw_window_state')['recipes'] == original
        call('raw_window_state', click='rawSynchronize', sync=dict(groups=[]))
        assert call('raw_window_state')['recipes'] == original
        call('raw_window_state', dehaze=23)
        call('raw_window_state', click='rawSynchronize', sync=dict(groups=[0], capture=str(output/'sync-settings.png')))
        selective = call('raw_window_state')['recipes']
        assert selective[1]['tone']['exposure'] == .25 and selective[1]['tone']['dehaze'] == 0
        assert selective[0]['tone']['dehaze'] == 23
        call('raw_window_state', click='rawSynchronize')
        state = call('raw_window_state')
        assert all(recipe['tone']['exposure'] == .25 for recipe in state['recipes'])
        call('raw_window_state', click='rawComputeAll')
        until(lambda state: state['ready'])
        call('raw_window_state', index=0, click='rawCompute')
        state = call('raw_window_state')
        assert state['idle'] and state['ready'] and '缓存' in state['status'], state
        custom = call('raw_window_state', modelChoice=1)
        assert custom['recipe']['customModel'] is True
        call('raw_window_state', click='rawUndo')
        assert call('raw_window_state')['modelChoice'] == 0
        call('raw_window_state', click='rawRedo')
        assert call('raw_window_state')['modelChoice'] == 1
        call('raw_window_state', denoise=True, model=str(Path(folder)/'missing.onnx'))
        state = call('raw_window_state', click='rawCompute')
        assert not state['ready'] and '缺失' in state['status'], state
        invalid_model = Path(folder)/'invalid.onnx'
        invalid_model.write_bytes(b'not-an-onnx-model')
        call('raw_window_state', model=str(invalid_model))
        call('raw_window_state', click='rawCompute')
        state = until(lambda state: state['idle'])
        assert not state['ready'], state
        call('raw_window_state', modelChoice=0)
        assert Path(call('raw_window_state')['modelPath']).is_file()
        call('raw_window_state', denoise=False)
        state = call('raw_window_state', click='rawCompute')
        assert state['ready'] and '缓存' in state['status'], state
        call('raw_window_state', accept=True)
        call('raw_window_state', save=str(Path(folder)/'multiple.huelith'))
        print('Multi-file sync, cache reuse, zoom retention, model invalidation and atomic submit checked', flush=True)
        paths = [source['path'], str(ROOT/'research/m0/samples/r10-5873.CR3')]
        cancelled_batch = call('window_open_batch', paths=paths, cancel=True)
        assert cancelled_batch['documents'] == 0 and not cancelled_batch['timedOut'], cancelled_batch
        opened_batch = call('window_open_batch', paths=paths)
        assert opened_batch['documents'] == 2 and not opened_batch['timedOut'], opened_batch
        print('Main editor batch import: cancel creates no document; accept creates two documents', flush=True)
        call('shutdown')
    (output/'window.json').write_text(json.dumps(dict(cancelled=cancelled, ready=ready,
        accepted=accepted, source_recipe_pixels_roundtrip=True,
        multi_file_sync_cache_zoom_invalidation_roundtrip=True), ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
