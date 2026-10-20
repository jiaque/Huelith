"""Effect-chain pixels, persistence and real Qt dialog through production DLL."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import traceback
import subprocess
import sys
from contextlib import nullcontext
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    qt = Path(r'D:\Program Files\Qt\6.10.3\msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows'
    dll = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = dll.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p
    out = ROOT/'docs/test-evidence/M2/effects'
    out.mkdir(parents=True, exist_ok=True)
    results = []

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
        print(results[-1], flush=True)

    def cube(fn):
        return 'LUT_3D_SIZE 2\n' + '\n'.join(' '.join(map(str, fn(r, g, b))) for b in (0, 1) for g in (0, 1) for r in (0, 1))

    identity = cube(lambda r, g, b: (r, g, b))
    swap = cube(lambda r, g, b: (b, g, r))
    red = cube(lambda r, g, b: (1, 0, 0))
    rgba = [.125, .25, .5, .5]

    child = len(sys.argv) > 1 and sys.argv[1] == '--library-check'
    with (nullcontext(sys.argv[2]) if child else tempfile.TemporaryDirectory(prefix='huelith-effects-')) as directory:
        folder = Path(directory)
        call('init', settings=directory)
        if child:
            call('window')
            call('window_scene', width=240, height=180, layers=[dict(name='grey', path=str(folder/'grey.png'))])
            call('window_action', text='智能效果…', dialog=dict(accept=True, steps=[
                dict(control='effectLibrarySearch', value=sys.argv[3]), dict(click='effectLibraryApply')]))
            nodes = call('window_probe')['layers'][0]['effects']
            assert len(nodes) == 1 and nodes[0]['amount'] == .65 and nodes[0]['lut'], nodes
            call('shutdown')
            return

        def probe(effects, color=rgba, **kw):
            return call('effect_probe', rgba=color, effects=effects, **kw)['pixels']

        def same(a, b, tolerance=.00015):
            assert len(a) == len(b) and max(abs(x-y) for x, y in zip(a, b)) < tolerance, (a, b)

        def identity_hdr():
            same(probe([dict(lut=identity)]), rgba*2)
            hdr = [.5, 1, 2, .5]
            same(probe([dict(lut=identity)], hdr), hdr*2, .003)
        test('cube_identity_trilinear_hdr_alpha', identity_hdr)

        def order():
            a = probe([dict(lut=red), dict(lut=swap)])
            b = probe([dict(lut=swap), dict(lut=red)])
            assert a != b and a[3] == b[3] == .5
            same(probe([dict(lut=red, enabled=False), dict(lut=swap, amount=0)]), rgba*2)
        test('node_order_changes_pixels_disabled_zero_strength_identity', order)

        def coverage():
            effect = dict(tone=dict(version=2, exposure=1), amount=.5, mask=[255, 0])
            expected = [v*1.5 for v in rgba[:3]] + [.5] + rgba
            same(probe([effect], save=str(folder/'masked.huelith')), expected)
            same(probe([dict(lut=swap)], [0, 0, 0, 0]), [0]*8)
        test('mask_and_strength_preserve_outside_and_alpha_persist', coverage)

        def persistence():
            effects = [dict(lut=swap), dict(tone=dict(version=2, exposure=.3), amount=.8), dict(lut=red, enabled=False)]
            probe(effects, save=str(folder/'多效果.huelith'))
        test('multi_effect_embedded_lut_save_reopen_pixel_exact', persistence)

        def invalid():
            for text in ('', 'LUT_3D_SIZE 1\n', identity.replace('0 0 0', 'nan 0 0'), identity+'\n0 0 0', 'DOMAIN_MIN 1 1 1\n'+identity):
                try:
                    probe([dict(lut=text)])
                except RuntimeError:
                    continue
                raise AssertionError('Invalid LUT accepted: '+text)
            try:
                probe([dict(lut=identity)], cancelled=True)
            except RuntimeError as error:
                assert '取消' in str(error)
            else:
                raise AssertionError('Cancellation ignored')
        test('reject_invalid_lut_and_cancel', invalid)

        call('window')
        paths = [entry['path'] for entry in call('window_chrome')['menus']]
        assert any('滤镜' in path and path.endswith('智能效果…') for path in paths), paths
        path = folder/'grey.png'
        Image.new('RGB', (240, 180), (100, 100, 100)).save(path)

        def scene():
            call('window_scene', width=240, height=180, layers=[dict(name='grey', path=str(path))])

        def effects():
            return call('window_probe')['layers'][0]['effects']

        def dialog(steps, accept=True):
            return call('window_action', text='智能效果…', dialog=dict(steps=steps, accept=accept))

        def ui():
            scene()
            before = call('window_interaction_state')['revision']
            steps = [dict(click='effectAdd'), dict(control='toneExposure', value=1), dict(click='effectDuplicate'),
                     dict(control='toneExposure', value=-.5), dict(click='effectUp'), dict(checked=False)]
            dialog(steps, False)
            assert not effects() and call('window_interaction_state')['revision'] == before
            dialog(steps)
            nodes = effects()
            assert len(nodes) == 2 and nodes[0]['tone']['exposure'] == -.5 and not nodes[0]['enabled'], nodes
            assert nodes[1]['tone']['exposure'] == 1 and nodes[1]['enabled'] and nodes[0]['id'] != nodes[1]['id']
            value = call('window_probe', x=100, y=100)['rgba']
            assert value[0] > 100, (value, nodes)
            assert call('window_interaction_state')['revision'] == before+1
            call('window_key', key='Ctrl+Z')
            assert not effects() and call('window_probe', x=100, y=100)['rgba'] == [100, 100, 100, 255]
            call('window_key', key='Ctrl+Shift+Z')
            assert effects() == nodes
            dialog([dict(row=0), dict(click='effectDelete')])
            assert len(effects()) == 1
        test('real_dialog_cancel_add_duplicate_reorder_toggle_delete_single_undo', ui)

        def selection():
            scene()
            call('window_key', key='M')
            for phase, x, y in [('press', 30, 30), ('move', 80, 80), ('release', 80, 80)]:
                call('window_mouse', phase=phase, x=x, y=y)
            dialog([dict(click='effectAdd'), dict(control='toneExposure', value=1), dict(click='effectSelection')])
            assert effects()[0]['mask']
            value = call('window_probe', x=50, y=50)['rgba']
            assert value[0] > 100, (value, effects())
            assert call('window_probe', x=100, y=100)['rgba'][0] == 100
            dialog([dict(click='effectInvert')])
            assert call('window_probe', x=50, y=50)['rgba'][0] == 100
            assert call('window_probe', x=100, y=100)['rgba'][0] > 100
            dialog([dict(click='effectClearMask')])
            assert not effects()[0]['mask']
        test('real_selection_effect_mask_invert_and_clear', selection)
        def preset():
            scene()
            lut_path = folder/'red.cube'
            lut_path.write_text(red, encoding='utf-8')
            preset_path = folder/'红色.hfx'
            dialog([dict(click='effectLut', file=str(lut_path)), dict(control='effectAmount', value=40),
                    dict(click='effectSavePreset', file=str(preset_path))])
            assert effects()[0]['lut'] and effects()[0]['amount'] == .4
            assert json.loads(preset_path.read_text(encoding='utf-8'))['lut'] == lut_path.read_bytes().decode('utf-8')
            expected = call('window_probe', x=100, y=100)['rgba']
            scene()
            dialog([dict(click='effectLoadPreset', file=str(preset_path))])
            assert effects()[0]['lut'] and effects()[0]['amount'] == .4
            assert call('window_probe', x=100, y=100)['rgba'] == expected
        test('real_lut_import_and_named_preset_save_load_pixels', preset)
        def library():
            scene()
            lut_path = folder/'library.cube'
            lut_path.write_text(swap, encoding='utf-8')
            name = '验证预设-' + folder.name
            dialog([dict(click='effectLut', file=str(lut_path)),
                    dict(control='effectAmount', value=65),
                    dict(control='effectLibraryName', value=name),
                    dict(click='effectLibrarySave')])
            expected = call('window_probe', x=100, y=100)['rgba']
            subprocess.run([sys.executable, __file__, '--library-check', directory, name], check=True, timeout=30)
            scene()
            dialog([dict(control='effectLibrarySearch', value=name),
                    dict(click='effectLibraryApply')])
            assert len(effects()) == 1 and effects()[0]['amount'] == .65 and effects()[0]['lut']
            assert call('window_probe', x=100, y=100)['rgba'] == expected
            scene()
            dialog([dict(control='effectLibrarySearch', value=name),
                    dict(control='effectLibraryName', value=name+'重命名'),
                    dict(click='effectLibraryRename')], False)
            dialog([dict(control='effectLibrarySearch', value=name+'重命名'),
                    dict(click='effectLibraryApply'), dict(click='effectLibraryDelete')])
            assert effects()[0]['name'] == name+'重命名'
            scene()
            dialog([dict(control='effectLibrarySearch', value=name), dict(click='effectLibraryApply')])
            assert not effects()
        test('managed_library_embedded_lut_reopen_search_rename_apply_delete', library)
        call('shutdown')
    report = dict(tests=results, passed=sum(x['passed'] for x in results), total=len(results))
    (out/'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    assert report['passed'] == report['total']


if __name__ == '__main__':
    main()
