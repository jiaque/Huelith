"""Measure real RAW dialog responsiveness through the production DLL, no test EXE."""
import ctypes
import json
import os
from pathlib import Path
import statistics
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt / 'bin')),
               os.add_dll_directory(str(ROOT / 'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt / 'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows'
    lib = ctypes.CDLL(str(ROOT / 'build/windows-release/huelith_python_driver.dll'))
    fn = lib.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p
    output = ROOT / 'docs/test-evidence/M2/raw-responsive'
    output.mkdir(parents=True, exist_ok=True)
    report = {}

    def call(op, **kw):
        result = json.loads(fn(json.dumps(dict(op=op, **kw)).encode()))
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result

    def until(predicate, timeout=90):
        deadline = time.perf_counter() + timeout
        while time.perf_counter() < deadline:
            call('pump', waitMs=16)
            state = call('raw_window_state')
            if predicate(state):
                return state
        raise TimeoutError(state)

    def settled(state):
        return state['previewPresented'] > 0 and state['previewPresented'] == state['previewRequested']

    with tempfile.TemporaryDirectory(prefix='huelith-responsive-') as folder:
        call('init', settings=folder)
        call('window')
        start = time.perf_counter()
        call('raw_window', path=str(ROOT / 'research/m0/samples/r10-5871.CR3'), denoise=False)
        state = until(lambda s: s['ready'] and settled(s))
        report['initial_preview_seconds'] = time.perf_counter() - start
        assert state['modelChoice'] == 0 and Path(state['modelPath']).is_file(), state
        report['builtin_model_selected'] = state['modelPath']
        latency, pump_latency, frames = [], [], set()
        for i in range(50):
            start = time.perf_counter()
            state = call('raw_window_state', exposure=-1 + i * .04)
            latency.append((time.perf_counter() - start) * 1000)
            start = time.perf_counter()
            call('pump', waitMs=25)
            pump_latency.append((time.perf_counter() - start) * 1000)
            frames.add(call('raw_window_state')['previewPresented'])
        start = time.perf_counter()
        state = until(settled)
        report['last_exposure_settle_ms'] = (time.perf_counter() - start) * 1000
        report['exposure_input_max_ms'] = max(latency)
        report['exposure_input_median_ms'] = statistics.median(latency)
        report['event_pump_max_ms'] = max(pump_latency)
        report['distinct_presented_frames_during_drag'] = len(frames)
        report['event_pump_samples_ms'] = pump_latency
        (output / 'latest-attempt.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        assert max(latency) < 100 and max(pump_latency) < 250, report
        assert len(frames) >= 5, report
        assert abs(state['recipe']['tone']['exposure'] - .96) < .06, state
        assert report['last_exposure_settle_ms'] < 1500, report
        drag = call('raw_slider_drag')
        until(settled)
        assert max(drag['eventMs']) < 100 and drag['heartbeatTicks'] >= 20, drag
        assert len(set(drag['presented'])) >= 5, drag
        report['actual_slider_drag'] = drag
        undone = call('raw_window_state', click='rawUndo')
        assert abs(undone['recipe']['tone']['exposure'] - .96) < .06, undone
        redone = call('raw_window_state', click='rawRedo')
        assert redone['recipe']['tone']['exposure'] == drag['exposure'], redone
        report['slider_one_drag_one_undo'] = True
        call('raw_window_state', section=6, dehaze=35)
        start = time.perf_counter()
        state = until(settled)
        report['dehaze_settle_ms'] = (time.perf_counter() - start) * 1000
        assert state['recipe']['tone']['dehaze'] == 35
        call('raw_window_state', capture=str(output / 'raw-effects.png'))
        call('raw_window_state', click='rawZoom100', dehaze=0)
        until(lambda s: settled(s) and s['previewFullResolution'])
        report['zoom100_full_resolution_refinement'] = True
        call('raw_window_state', click='rawZoomFit')
        until(settled)
        call('raw_window_state', exposure=0, dehaze=0)
        until(settled)
        start = time.perf_counter()
        call('raw_window_state', accept=True, asyncAccept=True)
        report['submit_dispatch_ms'] = (time.perf_counter() - start) * 1000
        state = until(lambda s: s['result'] == 1)
        report['full_resolution_commit_seconds'] = time.perf_counter() - start
        call('raw_window_state', save=str(Path(folder) / 'raw-responsive.huelith'))
        report['full_resolution_source_recipe_roundtrip'] = True
        call('raw_window', path=str(ROOT / 'research/m0/samples/r10-5871.CR3'), denoise=False)
        until(lambda s: s['ready'] and settled(s))
        call('raw_window_state', dehaze=60)
        until(settled)
        call('raw_window_state', accept=True, asyncAccept=True)
        start = time.perf_counter()
        call('raw_window_state', reject=True)
        cancelled = until(lambda s: not s['visible'])
        assert cancelled['result'] == 0
        report['cancel_full_resolution_seconds'] = time.perf_counter() - start
        assert report['cancel_full_resolution_seconds'] < 3
        call('shutdown')
    report['passed'] = True
    (output / 'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)


if __name__ == '__main__':
    main()
