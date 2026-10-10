"""Production RAW local masks: numerical boundaries plus real viewport gestures."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import time
import traceback

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
    results = []
    output = ROOT / 'docs/test-evidence/M2/raw'
    output.mkdir(parents=True, exist_ok=True)

    def call(op, **kw):
        result = json.loads(fn(json.dumps(dict(op=op, **kw)).encode()))
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result

    def test(name, body):
        try:
            body()
            results.append(dict(name=name, passed=True))
        except Exception:
            results.append(dict(name=name, passed=False, error=traceback.format_exc()))
        print(json.dumps(results[-1], ensure_ascii=False), flush=True)

    def probe(components, points, **extra):
        recipe = dict(locals=[dict(tone=dict(version=2, exposure=1), components=components)], **extra)
        return call('raw_local_probe', recipe=recipe, points=points)

    def brush():
        stamp = dict(mode='brush', radius=.2, feather=0, points=[dict(x=.5, y=.5)])
        result = probe([stamp], [[50, 40], [1, 1]])['samples']
        assert result[0]['mask'] == 255 and abs(result[0]['rgba'][0] - .4) < 1e-5
        assert result[1]['mask'] == 0 and abs(result[1]['rgba'][0] - .2) < 1e-5
        assert all(s['rgba'][3] == .5 for s in result)
        erased = probe([stamp, dict(stamp, radius=.06, subtract=True)], [[50, 40], [60, 40]])['samples']
        assert erased[0]['mask'] == 0 and erased[1]['mask'] == 255

    test('brush_subtract_outside_unchanged_alpha_preserved', brush)

    def gradients():
        linear = probe([dict(mode='linear', x=.2, y=.5, endX=.8, endY=.5)], [[5, 40], [50, 40], [95, 40]])['samples']
        assert linear[0]['mask'] == 255 and 110 < linear[1]['mask'] < 140 and linear[2]['mask'] == 0
        radial = probe([dict(mode='radial', x=.2, y=.2, endX=.8, endY=.8, feather=.5)], [[50, 40], [1, 1]])['samples']
        assert radial[0]['mask'] == 255 and radial[1]['mask'] == 0

    test('linear_radial_ranges_feather', gradients)

    def crop():
        result = probe([], [[1, 1]], crop=dict(x=.2, y=.25, endX=.8, endY=.75))
        assert (result['width'], result['height']) == (60, 40)
        try:
            probe([], [], crop=dict(x=.5, y=.5, endX=.5, endY=.5))
        except RuntimeError:
            return
        raise AssertionError('empty crop accepted')

    test('normalized_crop_rejects_empty', crop)

    with tempfile.TemporaryDirectory(prefix='huelith-local-') as folder:
        call('init', settings=folder)
        call('window')

        def window():
            call('raw_window', path=str(ROOT / 'research/m0/samples/r10-5871.CR3'), denoise=False)
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                call('pump', waitMs=30)
                if call('raw_window_state')['ready'] and call('raw_window_state')['previewPresented'] > 0:
                    break
            assert call('raw_window_state')['ready']
            call('raw_window_state', click='rawAddLocal')
            result = call('raw_window_state', stroke=[[.4, .4], [.45, .45], [.5, .5]], exposure=1)
            local = result['recipe']['locals'][0]
            assert local['tone']['exposure'] == 1 and len(local['components']) == 1
            call('raw_window_state', mode=2, stroke=[[.45, .45], [.46, .46]])
            result = call('raw_window_state')
            assert result['recipe']['locals'][0]['components'][-1]['subtract']
            result = call('raw_window_state', click='rawUndo')
            assert len(result['recipe']['locals'][0]['components']) == 1 and result['target'] == 1
            result = call('raw_window_state', click='rawRedo')
            assert len(result['recipe']['locals'][0]['components']) == 2
            result = call('raw_window_state', mode=5, stroke=[[.2, .2], [.8, .8]])
            assert 'crop' in result['recipe']
            call('raw_window_state', capture=str(output / 'raw-local-crop-window.png'))
            result = call('raw_window_state', accept=True)
            assert result['result'] == 1
            call('raw_window_state', save=str(Path(folder) / 'local-crop.huelith'))

        test('raw_viewport_local_erase_undo_redo_crop_commit_persistence', window)
        call('shutdown')
    report = dict(passed=sum(t['passed'] for t in results), total=len(results), tests=results)
    (output / 'local-results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    return int(report['passed'] != report['total'])


if __name__ == '__main__':
    raise SystemExit(main())
