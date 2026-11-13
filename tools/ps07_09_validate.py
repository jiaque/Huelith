"""Bounded validation of the PS07–09 additions; does not certify PS10–12."""
import base64
import ctypes
import json
import os
import struct
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
QT = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
handles = [os.add_dll_directory(str(QT / 'bin')), os.add_dll_directory(str(ROOT / 'build/windows-release'))]
os.environ['QT_PLUGIN_PATH'] = str(QT / 'plugins')
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
lib = ctypes.CDLL(str(ROOT / 'build/windows-release/huelith_python_driver.dll'))
fn = lib.huelith_python_command
fn.argtypes = [ctypes.c_char_p]
fn.restype = ctypes.c_char_p


def call(op, **args):
    value = json.loads(fn(json.dumps(dict(op=op, **args), ensure_ascii=False).encode()))
    if 'error' in value:
        raise RuntimeError(value)
    return value


with tempfile.TemporaryDirectory(prefix='ps07-09-', dir=ROOT / 'build', ignore_cleanup_errors=True) as folder:
    call('init', settings=folder)
    try:
        brush = dict(version=1, diameter=40, spacing=.2)
        stroke = dict(width=256, height=128, points=[[35, 64, 1], [210, 64, 1]], finish=True)
        plain = call('brush_probe', settings=brush, **stroke)
        dual_settings = dict(brush, dualEnabled=True, dualTip=2, dualScale=.6, dualDepth=1)
        dual = call('brush_probe', settings=dual_settings, **stroke)
        assert dual['hash'] != plain['hash']
        assert call('brush_probe', settings=dual['settings'], **stroke)['hash'] == dual['hash']
        tilted = dict(stroke, points=[[35, 64, 1, 60, 20], [210, 64, 1, 60, 20]])
        tilt = call('brush_probe', settings=dict(brush, tiltRoundness=True), **tilted)
        assert tilt['hash'] != plain['hash']
        mix = dict(brush, mixerEnabled=True, mixerWet=.5, mixerLoad=.5)
        red = call('brush_probe', settings=mix, pigment='#ff0000', **stroke)
        green = call('brush_probe', settings=mix, pigment='#00ff00', **stroke)
        assert red['hash'] != green['hash']
        assert call('brush_probe', settings=red['settings'], pigment='#ff0000', **stroke)['hash'] == red['hash']
        # A 2x2 sampled v1 tip, plus explicit rejection of unsupported versions.
        payload = struct.pack('>IHB4h4iHB', 0, 20, 1, 0, 0, 2, 2, 0, 0, 2, 2, 8, 0) + bytes([0, 255, 128, 64])
        abr = struct.pack('>HHHI', 1, 1, 2, len(payload)) + payload
        assert len(call('brush_probe', abr=base64.b64encode(abr).decode())['presets']) == 1
        for invalid in (abr[:-1], struct.pack('>HH', 6, 1)):
            result = json.loads(fn(json.dumps(dict(op='brush_probe', abr=base64.b64encode(invalid).decode())).encode()))
            assert 'error' in result
        print('PASS brush: dual, tilt input, pigment mixing, deterministic replay, ABR boundaries', flush=True)

        for kind in range(1, 17):
            effect = dict(filter=kind, filterRadius=5, filterAngle=30)
            result = call('effect_probe', rgba=[.2, .3, .4, 1], effects=[effect], save=str(Path(folder) / f'filter-{kind}.huelith'))
            assert result
        print('PASS effects: sixteen filters render and survive project round trip', flush=True)
        suite = call('brush_probe', filterSuite=True, file=str(Path(folder) / 'filters.huelith'))
        assert all(suite['changed']) and suite['sharedSource'], suite
        print('PASS varied-image filters, transparent boundary, source sharing and undo/redo', flush=True)

        text = dict(version=2, html='<p style="font-size:24pt;color:#ffffff">中文 Office</p>', width=240, height=240)
        hashes = []
        for layout in range(3):
            item = dict(text, layout=layout, ligatures=True)
            if layout == 2:
                item['baseline'] = [[0, 5, 90], [1, 230, 110]]
            before = call('design_probe', width=320, height=320, text=item)
            after = call('design_probe', width=320, height=320, text=item, file=str(Path(folder) / f'text-{layout}.huelith'))
            assert before['hash'] == after['hash']
            assert after['text']['layout'] == layout
            hashes.append(before['hash'])
        assert len(set(hashes)) == 3
        base = call('design_probe', text=text)
        styles = base['styles']
        style_count = len(styles['entries'])
        styles['entries'][0]['enabled'] = True
        styles['entries'][0]['color'] = '#ffff0000'
        styles['entries'].append(dict(styles['entries'][0], size=8, color='#ff00ff00'))
        styles['scale'] = 1.5
        styled = call('design_probe', text=text, styles=styles)
        restored = call('design_probe', text=text, styles=styles, file=str(Path(folder) / 'styles.huelith'))
        assert styled['hash'] == restored['hash'] and styled['hash'] != base['hash']
        assert len(restored['styles']['entries']) == style_count + 1
        print('PASS text and styles: horizontal/vertical/path, multiple style instances, save/reopen', flush=True)
    finally:
        call('shutdown')
