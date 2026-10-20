"""Calibration math oracle, legacy recipes, production RAW controls and persistence.

Uses the existing Python -> production DLL / Qt test path, never a test executable.
The numerical reference describes Huelith's process; it is not an Adobe image-quality oracle.
"""
import ctypes
import itertools
import json
import os
from pathlib import Path
import tempfile
import time
import traceback
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/test-evidence/M2/calibration'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')),
               os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows'
    dll = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = dll.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p

    def raw(op, **values):
        return json.loads(fn(json.dumps(dict(op=op, **values)).encode()))

    def call(op, **values):
        result = raw(op, **values)
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result

    results = []

    def test(name, body):
        try:
            body()
            results.append(dict(name=name, passed=True))
            print('PASS', name, flush=True)
        except Exception:
            results.append(dict(name=name, passed=False, error=traceback.format_exc()))
            print(results[-1]['error'], flush=True)

    def pump():
        for _ in range(4):
            call('pump', waitMs=100)

    with tempfile.TemporaryDirectory(prefix='huelith-calibration-') as folder:
        call('init', settings=folder)

        def identity():
            for rgb in ([0, 0, 0], [.18]*3, [-.1, 2, 5], [1, 0, 0]):
                assert call('calibration_probe', rgb=rgb)['rgb'] == rgb
        test('zero_is_exact_identity_including_hdr_negative', identity)

        def oracle():
            basis = np.array([[.4124564, .3575761, .1804375],
                              [.2126729, .7151522, .0721750],
                              [.0193339, .1191920, .9503041]])
            white = basis.sum(axis=1)
            wxy = white[:2]/white.sum()
            xy = basis[:2]/basis.sum(axis=0)
            rng = np.random.default_rng(621)
            for _ in range(64):
                hue = rng.uniform(-100, 100, 3)
                sat = rng.uniform(-100, 100, 3)
                adjusted = np.zeros((3, 3))
                for c in range(3):
                    angle = np.deg2rad(hue[c]*.15)
                    rotation = np.array([[np.cos(angle), -np.sin(angle)],
                                         [np.sin(angle), np.cos(angle)]])
                    adjusted[:2, c] = wxy + np.exp2(sat[c]/200)*rotation@(xy[:, c]-wxy)
                    adjusted[2, c] = 1-adjusted[:2, c].sum()
                adjusted *= np.linalg.solve(adjusted, white)
                expected = np.linalg.solve(basis, adjusted)
                rgb = rng.uniform(-.2, 4, 3)
                actual = call('calibration_probe', rgb=rgb.tolist(),
                              settings=dict(hue=hue.tolist(), saturation=sat.tolist()))
                assert np.max(np.abs(np.array(actual['matrix'])-expected)) < 2e-12
                assert np.max(np.abs(np.array(actual['rgb'])-expected@rgb)) < 2e-11
                assert np.max(np.abs(expected@np.ones(3)-1)) < 2e-12
        test('independent_numpy_xyz_reference_64_cases_preserves_white', oracle)

        def limits():
            for h in itertools.product([-100, 100], repeat=3):
                for s in itertools.product([-100, 100], repeat=3):
                    result = call('calibration_probe', rgb=[.18]*3,
                                  settings=dict(hue=h, saturation=s))['rgb']
                    assert np.max(np.abs(np.array(result)-.18)) < 1e-10
            for settings in [dict(processVersion=2), dict(processVersion=1.5),
                             dict(hue=[0, 0]), dict(hue=[0, 101, 0]),
                             dict(shadowTint='0'), dict(saturation=[False, 0, 0]), dict(unknown=1)]:
                assert 'error' in raw('calibration_probe', rgb=[1, 1, 1], settings=settings), settings
        test('all_primary_extreme_corners_and_malformed_recipe_rejection', limits)

        def shadow():
            ratios = []
            for light in [.01, .18, 4]:
                positive = call('calibration_probe', rgb=[light]*3,
                                settings=dict(shadowTint=60))['rgb']
                negative = call('calibration_probe', rgb=[light]*3,
                                settings=dict(shadowTint=-60))['rgb']
                assert positive[0] > positive[1] and negative[0] < negative[1]
                assert abs(np.dot(positive, [.2126, .7152, .0722])-light) < 1e-10
                ratios.append(positive[0]/positive[1])
            assert ratios[0] > ratios[1] > ratios[2]
        test('shadow_tint_direction_luma_preservation_highlight_falloff', shadow)

        def persistence():
            rgba = [.1, .2, .6, .5]
            calibration = dict(shadowTint=-23, hue=[21, -18, 13], saturation=[-9, 17, 28])
            settings = dict(version=4, calibration=calibration)
            result = call('tone_probe', settings=settings, rgba=rgba,
                          save=str(Path(folder)/'calibration.huelith'))['rgba']
            assert result[3] == .5 and np.max(np.abs(np.array(result[:3])-rgba[:3])) > .005
            settings['sections'] = [True]*8+[False]
            assert call('tone_probe', settings=settings, rgba=rgba)['rgba'] == call(
                'tone_probe', settings=dict(version=3), rgba=rgba)['rgba']
            for version in [1, 2, 3]:
                legacy = dict(version=version, sections=[True]*8)
                assert call('tone_probe', settings=legacy, rgba=rgba)['rgba'] == call(
                    'tone_probe', settings=dict(version=4), rgba=rgba)['rgba']
            assert 'error' in raw('tone_probe', settings=dict(version=4, calibration=[]), rgba=rgba)
        test('float_alpha_project_roundtrip_group_bypass_legacy_versions', persistence)

        def window():
            call('window')
            sample = str(ROOT/'research/m0/samples/r10-5871.CR3')
            call('raw_window', paths=[sample, sample], denoise=False)
            deadline = time.monotonic()+60
            while time.monotonic() < deadline:
                call('pump', waitMs=40)
                if call('raw_window_state')['previewPresented'] > 0:
                    break
            else:
                raise TimeoutError('RAW preview not ready')
            state = call('raw_window_state', section=8, inventory=True)
            names = {w['name'] for w in state['controls']}
            required = {'toneCalibrationShadowTint', 'rawCalibrationProcess', 'rawSectionEnable8'}
            required |= {f'toneCalibration{p}{f}' for p in ('Red', 'Green', 'Blue')
                         for f in ('Hue', 'Saturation')}
            assert required <= names, required-names
            state = call('raw_window_state', control='toneCalibrationRedHue', value=37)
            assert state['recipe']['tone']['calibration']['hue'][0] == 37
            pump()
            state = call('raw_window_state', click='rawUndo')
            assert state['recipe'].get('tone', {}).get('calibration', {}).get('hue', [0])[0] == 0
            state = call('raw_window_state', click='rawRedo')
            assert state['recipe']['tone']['calibration']['hue'][0] == 37
            state = call('raw_window_state', click='rawSectionEnable8')
            assert not state['recipe']['tone']['sections'][8]
            assert state['recipe']['tone']['calibration']['hue'][0] == 37
            call('raw_window_state', click='rawSectionEnable8')
            call('raw_window_state', click='rawSynchronize', sync=dict(groups=[], cancel=False))
            state = call('raw_window_state')
            assert state['recipes'][1].get('tone', {}).get('calibration', {}).get('hue', [0])[0] == 0
            call('raw_window_state', click='rawSynchronize', sync=dict(groups=[10]))
            state = call('raw_window_state')
            assert state['recipes'][1]['tone']['calibration']['hue'][0] == 37
            assert state['recipes'][1]['tone']['sections'][8]
            # The existing generic slider gesture sends real Qt mouse events, including double click.
            state = call('raw_hsl', slider='toneCalibrationGreenSaturationSlider', position=.7)
            assert abs(state['recipe']['tone']['calibration']['saturation'][1]-40) <= 1
            call('raw_hsl', slider='toneCalibrationGreenSaturationSlider', reset=True)
            call('raw_window_state', click='rawSection4')
            call('raw_window_state', section=8)
            pump()
            call('raw_window_state', capture=str(OUT/'calibration-window.png'))
            call('raw_window_state', index=1, included=False)
            state = call('raw_window_state', index=0, accept=True)
            assert state['result'] == 1, state
            call('raw_window_state', save=str(Path(folder)/'raw-calibration.huelith'))
        test('actual_raw_controls_undo_group_sync_slider_and_source_roundtrip', window)
        call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
    assert all(r['passed'] for r in results), results
    print(f'{len(results)}/{len(results)} passed')


if __name__ == '__main__':
    main()
