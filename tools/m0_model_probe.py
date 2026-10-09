"""M0 research only: compare original UtNet2 weights with published ONNX.

Run with the isolated build/m0-python interpreter. Assets are local and hashed;
this script never downloads or executes checkpoint pickle code.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time
import types

import numpy as np
import onnxruntime as ort
import torch


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--raw', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    torch.set_num_threads(4)
    torch.manual_seed(42)
    for name in ('rawnind', 'rawnind.libs', 'rawnind.libs.rawproc'):
        sys.modules[name] = types.ModuleType(name)
    source = args.assets / 'rawnind/src/rawnind/models/raw_denoiser.py'
    spec = importlib.util.spec_from_file_location('m0_upstream_utnet2', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    model = module.UtNet2(in_channels=4, funit=32, activation='LeakyReLU', preupsample=False)
    checkpoint = args.assets / 'utnet2-bayer.pt'
    model.load_state_dict(torch.load(checkpoint, map_location='cpu', weights_only=True), strict=True)
    model.eval()
    onnx_path = args.assets / 'model-package/rawdenoise-nind/model_bayer.onnx'
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(str(onnx_path), options, providers=['CPUExecutionProvider'])
    report = dict(torch=torch.__version__, onnxruntime=ort.__version__, providers=session.get_providers(),
                  checkpoint_sha256=sha256(checkpoint), onnx_sha256=sha256(onnx_path),
                  source_sha256=sha256(source), tests=[])
    inputs = {'controlled_random': np.random.default_rng(42).random((1,4,512,512), dtype=np.float32)}
    if args.raw:
        import rawpy
        with rawpy.imread(str(args.raw)) as raw:
            image = raw.raw_image_visible.astype(np.float32)
            pattern = raw.raw_pattern
            assert pattern.shape == (2,2), 'Bayer required'
            colors = raw.color_desc.decode('ascii').rstrip('\x00')
            channels = []
            used = set()
            # G1/G2 ordered by the original Bayer rows, then force RGGB packing.
            for letter in 'RGGB':
                choices = [(y,x) for y in range(2) for x in range(2)
                           if colors[int(pattern[y,x])] == letter and (y,x) not in used]
                y,x = choices[0]; used.add((y,x)); index = int(pattern[y,x])
                plane = image[y::2,x::2]
                black = raw.black_level_per_channel[index]
                channels.append(np.clip((plane-black)/(raw.white_level-black), 0, 1))
            h = min(c.shape[0] for c in channels); w = min(c.shape[1] for c in channels)
            packed = np.stack([c[:h,:w] for c in channels])
            y = max(0, (h-512)//2); x = max(0,(w-512)//2)
            inputs['r10_center'] = np.ascontiguousarray(packed[None,:,y:y+512,x:x+512])
            report['raw'] = dict(file=args.raw.name, sha256=sha256(args.raw), visible_size=list(image.shape),
                pattern=pattern.tolist(), colors=colors, black=raw.black_level_per_channel, white=raw.white_level,
                camera_whitebalance=raw.camera_whitebalance, rgb_xyz_matrix=raw.rgb_xyz_matrix.tolist())
    for name, data in inputs.items():
        start = time.perf_counter()
        with torch.inference_mode(): reference = model(torch.from_numpy(data)).numpy()
        torch_seconds = time.perf_counter()-start
        start = time.perf_counter(); output = session.run(None, {'input':data})[0]; ort_seconds=time.perf_counter()-start
        delta = np.abs(output-reference)
        # Predeclared deployment parity, separate from real-world denoising quality.
        passed = bool(np.all(np.isfinite(output)) and np.all(delta <= 1e-4+1e-4*np.abs(reference)))
        record=dict(name=name, shape=list(output.shape), max_abs=float(delta.max()), mean_abs=float(delta.mean()),
            p99_abs=float(np.quantile(delta,0.99)), reference_peak=float(np.abs(reference).max()),
            torch_seconds=torch_seconds, onnx_seconds=ort_seconds, passed=passed)
        report['tests'].append(record); print(json.dumps(record),flush=True)
        np.save(args.output/(name+'-onnx.npy'),output)
        np.save(args.output/(name+'-input.npy'),data)
    (args.output/'model-parity.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    if not all(t['passed'] for t in report['tests']): raise SystemExit(1)


if __name__ == '__main__':
    main()
