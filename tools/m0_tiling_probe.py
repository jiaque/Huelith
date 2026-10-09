"""Compare tiled real RAW crop with untiled original framework inference."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import types

import numpy as np
import onnxruntime as ort
import rawpy
import torch
from m0_raw_probe import pack,tiled

parser=argparse.ArgumentParser();parser.add_argument('--assets',type=Path,required=True)
parser.add_argument('--raw',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();torch.set_num_threads(4)
for name in ('rawnind','rawnind.libs','rawnind.libs.rawproc'):sys.modules[name]=types.ModuleType(name)
spec=importlib.util.spec_from_file_location('m0_utnet2',args.assets/'rawnind/src/rawnind/models/raw_denoiser.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
model=module.UtNet2(in_channels=4,funit=32,activation='LeakyReLU',preupsample=False)
model.load_state_dict(torch.load(args.assets/'utnet2-bayer.pt',map_location='cpu',weights_only=True),strict=True);model.eval()
with rawpy.imread(str(args.raw)) as raw: packed=pack(raw)[:,512:1280,768:1536]
with torch.inference_mode(): reference=model(torch.from_numpy(np.ascontiguousarray(packed[None]))).numpy()[0].transpose(1,2,0)
options=ort.SessionOptions();options.intra_op_num_threads=4
session=ort.InferenceSession(str(args.assets/'model-package/rawdenoise-nind/model_bayer.onnx'),options,providers=['CPUExecutionProvider'])
result,times=tiled(session,packed,False)
gain=float(packed.mean(dtype=np.float64)/reference.mean(dtype=np.float64))
delta=np.abs(result-reference)*abs(gain)
# Exclude external padding; keep internal tile joins in the comparison.
interior=delta[256:-256,256:-256]
seams=np.concatenate([delta[510:514,256:-256].ravel(),delta[1022:1026,256:-256].ravel(),
                      delta[256:-256,510:514].ravel(),delta[256:-256,1022:1026].ravel()])
report=dict(crop=list(packed.shape),halo_packed=128,core_packed=256,tiles=len(times),
    gain=gain,interior_max_abs=float(interior.max()),interior_mean_abs=float(interior.mean()),
    seam_max_abs=float(seams.max()),seam_mean_abs=float(seams.mean()),
    boundary_mean_abs=float(delta[:256].mean()),
    interior_pass=bool(interior.max()<=1e-3),criterion='gain-normalized interior max abs <= 0.001; external padding separately reported')
args.output.write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
if not report['interior_pass']:
    raise SystemExit(1)
