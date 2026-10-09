"""Verify the fixed v2 migration against existing v1 full-image evidence."""
import json
from pathlib import Path
import numpy as np
from m0_color import camera_to_rec2020, camera_to_srgb, XYZ_TO_REC2020

root=Path(__file__).resolve().parents[1]
evidence=root/'docs/test-evidence/M0/validation'
output=root/'docs/test-evidence/M0/closeout'
results=[]
for identifier,old_folder in [('5871','raw-full-dml'),('5873','raw-5873-dml'),('8185','raw-8185-dml')]:
    metadata=json.loads((output/f'raw-{identifier}-v2/raw-results.json').read_text())
    matrix=np.asarray(metadata['matrix']);wb=metadata['wb']
    previous=np.load(evidence/old_folder/'linear-rec2020.npy')
    current=np.load(output/f'raw-{identifier}-v2/linear-rec2020.npy')
    correction=camera_to_rec2020(matrix,wb) @ matrix @ np.linalg.inv(XYZ_TO_REC2020)
    reference=np.einsum('...c,dc->...d',previous,correction.astype(np.float32))
    error=np.abs(current-reference)
    passed=bool(np.isfinite(current).all() and np.all(error<=1e-5+1e-4*np.abs(reference)))
    results.append(dict(raw=identifier,passed=passed,max_abs=float(error.max()),
                        mean_abs=float(error.mean()),pipeline=metadata['color_pipeline']))
    del previous,current,reference,error
neutral=camera_to_srgb(matrix,[1,1,1]) @ np.ones(3)
if not np.allclose(neutral,1,atol=1e-10,rtol=0):raise ValueError('neutral axis failed')
invalid_rejected=False
try:camera_to_srgb(matrix,[0,1,1])
except ValueError:invalid_rejected=True
report=dict(results=results,neutral_error=float(np.max(np.abs(neutral-1))),
            invalid_white_balance_rejected=invalid_rejected,
            criterion='abs <= 1e-5 + 1e-4 * abs(reference)',
            limitation='Numeric migration consistency, not measured camera color accuracy.')
(output/'color-check.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report),flush=True)
if not invalid_rejected or not all(r['passed'] for r in results):raise SystemExit(1)
