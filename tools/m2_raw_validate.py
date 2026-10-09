"""Validate production LibRaw/ORT C++ against the frozen M0 Python float evidence."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import tempfile
import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sample', default='5871')
    parser.add_argument('--cpu', action='store_true')
    args = parser.parse_args()
    qt = Path(r'D:\Program Files\Qt\6.10.3\msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')),
               os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_QPA_PLATFORM'] = 'offscreen'
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    lib = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    function = lib.huelith_python_command
    function.argtypes = [ctypes.c_char_p]
    function.restype = ctypes.c_char_p
    def call(**request):
        result = json.loads(function(json.dumps(request).encode()))
        return result
    output = ROOT/'docs/test-evidence/M2/raw'
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='huelith-raw-') as directory:
        call(op='init', settings=directory)
        request = dict(op='raw_develop', path=str(ROOT/f'research/m0/samples/r10-{args.sample}.CR3'),
                       model=str(ROOT/'research/m0/model-package/rawdenoise-nind/model_bayer.onnx'),
                       denoise=True, directml=not args.cpu)
        assert '取消' in call(**request, cancelled=True)['error']
        assert '取消' in call(**request, cancelAfter=1)['error']
        print('Cancellation checked', flush=True)
        missing = {**request, 'model': str(Path(directory)/'missing.onnx')}
        assert '缺失' in call(**missing)['error']
        floating = Path(directory)/'linear.rgba32f'
        result = call(**request, floatOutput=str(floating),
                      preview=str(output/f'{args.sample}-preview.png'))
        if 'error' in result:
            raise RuntimeError(result['error'])
        image = np.memmap(floating, dtype=np.float32, mode='r',
                          shape=(result['height'], result['width'], 4))
        reference = np.load(ROOT/f'docs/test-evidence/M0/closeout/raw-{args.sample}-v2/linear-rec2020.npy',
                            mmap_mode='r')
        maximum, total = 0.0, 0.0
        for y in range(image.shape[0]):
            assert np.isfinite(image[y]).all() and np.all(image[y, :, 3] == 1)
            difference = np.abs(image[y, :, :3]-reference[y])
            maximum = max(maximum, float(difference.max()))
            total += float(difference.sum(dtype=np.float64))
        result['m0Comparison'] = dict(maximum=maximum, mean=total/reference.size,
                                     passed=maximum < .0001,
                                     reference='M0 normalized camera WB v2 DirectML')
        result['cancellation'] = 'before decode and after first tile discard output'
        del image, reference
        (output/f'{args.sample}-{"cpu" if args.cpu else "dml"}.json').write_text(
            json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
        call(op='shutdown')
        assert result['m0Comparison']['passed']


if __name__ == '__main__':
    main()
