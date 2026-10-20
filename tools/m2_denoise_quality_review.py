"""Screen frozen, production-matched RAW outputs for periodic tile boundaries.

This is not a clean-reference fidelity metric. The three existing M2 raw
sample reports must establish production equivalence before reusing M0 floats.
"""
import json
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
records = []
for sample in ('5871', '5873', '8185'):
    evidence = json.loads((ROOT / f'docs/test-evidence/M2/raw/{sample}-dml.json').read_text())
    assert evidence['m0Comparison']['passed']
    image = np.load(ROOT / f'docs/test-evidence/M0/closeout/raw-{sample}-v2/linear-rec2020.npy', mmap_mode='r')
    axes = []
    for axis in (0, 1):
        def difference(position):
            return float(np.mean(np.abs(np.take(image, position, axis=axis) -
                                        np.take(image, position - 1, axis=axis)), dtype=np.float64))
        lines = []
        for position in range(512, image.shape[axis], 512):
            nearby = np.mean([difference(position + offset) for offset in (-8, -4, -2, 2, 4, 8)
                              if 0 < position + offset < image.shape[axis]])
            boundary = difference(position)
            lines.append(dict(position=position, boundary=boundary, nearby=float(nearby),
                              ratio=float(boundary / max(nearby, 1e-12))))
        axes.append(dict(axis=axis, mean_ratio=float(np.mean([x['ratio'] for x in lines])),
                         max_ratio=max(x['ratio'] for x in lines), lines=lines))
    records.append(dict(sample=sample, axes=axes))
output = ROOT / 'docs/test-evidence/M2/denoise-strength/seam-review.json'
output.write_text(json.dumps(dict(source='Frozen M0 floats matched to production by M2/raw reports',
                                  note='Contrast screening, not clean-reference fidelity', samples=records), indent=2))
print(json.dumps([dict(sample=x['sample'], ratios=[a['mean_ratio'] for a in x['axes']]) for x in records]))
