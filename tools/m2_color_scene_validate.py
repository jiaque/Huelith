"""Finite natural-scene acceptance: production RAW preview at proxy and full scale."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import time
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/test-evidence/M2/color-scenes'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows'
    dll = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = dll.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p

    def call(op, **kw):
        result = json.loads(fn(json.dumps(dict(op=op, **kw)).encode()))
        assert 'error' not in result, result
        return result

    def wait(full=False):
        end = time.monotonic()+120
        while time.monotonic() < end:
            call('pump', waitMs=25)
            state = call('raw_window_state')
            if state['previewPresented'] > 0 and state['previewPresented'] == state['previewRequested']:
                if not full or state['previewFullResolution']:
                    return state
        raise AssertionError(state)

    def capture(name):
        prefix = str(OUT/name)
        call('raw_window_state', previewOutput=prefix)
        return Image.open(prefix+'rawPreview.png').convert('RGB')

    results = []
    with tempfile.TemporaryDirectory(prefix='huelith-color-') as directory:
        call('init', settings=directory)
        call('window')
        try:
            for sample in ['r10-5871.CR3', 'r10-5873.CR3', 'r10-8185.CR3']:
                path = ROOT/'research/m0/samples'/sample
                call('raw_window', path=str(path), denoise=False)
                wait()
                base = capture(sample+'-before-')
                started = time.monotonic()
                for control, value in [('rawExposure', .35), ('toneContrast', 12),
                                       ('toneSaturation', 8), ('toneClarity', 10), ('toneSharpening', 40)]:
                    call('raw_window_state', control=control, value=value)
                wait()
                proxy = capture(sample+'-proxy-')
                proxy_seconds = time.monotonic()-started
                call('raw_window_state', click='rawZoom100')
                wait(full=True)
                full = capture(sample+'-full-')
                reduced = full.resize(proxy.size, Image.Resampling.LANCZOS)
                a, b = np.asarray(proxy).astype(float), np.asarray(reduced).astype(float)
                delta = np.abs(a-b)
                change = np.mean(np.abs(a-np.asarray(base).astype(float)))
                assert change > 1, 'Controls did not change the natural image'
                # Detect a different color transform or stale render, while allowing spatial
                # effects to differ at subpixel scales. Save the actual differences for review.
                assert delta.mean() < 12 and np.quantile(delta, .99) < 65, (sample, delta.mean())
                sheet = Image.new('RGB', (1200, 420), '#222222')
                draw = ImageDraw.Draw(sheet)
                for index, (label, image) in enumerate([('Before', base), ('Adjusted proxy', proxy), ('Full render reduced', reduced)]):
                    image = image.copy(); image.thumbnail((400, 390))
                    sheet.paste(image, (index*400, 25))
                    draw.text((index*400+5, 5), label, fill='white')
                sheet.save(OUT/(sample+'-comparison.jpg'))
                results.append(dict(sample=sample, proxySeconds=proxy_seconds, meanDifference=float(delta.mean()),
                                    p99Difference=float(np.quantile(delta, .99)), changedMean=float(change),
                                    fullSize=full.size, passed=True))
                print(results[-1], flush=True)
                call('raw_window_state', reject=True)
                call('pump', waitMs=100)
        finally:
            call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
