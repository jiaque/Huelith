"""Isolated DirectML verification; CPU fallback is disabled for placement proof."""
import argparse
import json
from pathlib import Path
import sys
import time

import numpy as np

parser=argparse.ArgumentParser()
parser.add_argument('--runtime',type=Path,required=True)
parser.add_argument('--model',type=Path,required=True)
parser.add_argument('--reference',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
sys.path.insert(0,str(args.runtime.resolve()))
import onnxruntime as ort
report={'runtime':ort.__version__,'available':ort.get_available_providers()}
try:
    options=ort.SessionOptions();options.enable_mem_pattern=False
    options.execution_mode=ort.ExecutionMode.ORT_SEQUENTIAL
    options.add_session_config_entry('session.disable_cpu_ep_fallback','1')
    start=time.perf_counter()
    session=ort.InferenceSession(str(args.model),options,providers=[('DmlExecutionProvider',{'device_id':0})])
    session.disable_fallback()
    report['load_seconds']=time.perf_counter()-start
    data=np.load(args.reference/'r10_center-input.npy'); reference=np.load(args.reference/'r10_center-onnx.npy')
    start=time.perf_counter(); output=session.run(None,{'input':data})[0]
    delta=np.abs(output-reference)
    report.update(providers=session.get_providers(),seconds=time.perf_counter()-start,
        finite=bool(np.isfinite(output).all()),max_abs=float(delta.max()),mean_abs=float(delta.mean()),
        passed=bool(np.isfinite(output).all() and np.all(delta<=1e-4+1e-4*np.abs(reference))))
except Exception as error:
    report.update(passed=False,error=str(error))
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report),flush=True)
if not report['passed']: raise SystemExit(1)
