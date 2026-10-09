"""Fixed three-run M0 resource experiment; no long-running stress loop."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import gc
import json
import os
from pathlib import Path
import time

import numpy as np
import onnxruntime as ort
import psutil
import rawpy
from m0_raw_probe import pack, tiled


class CounterValue(c.Structure):
    _fields_ = [('status', w.DWORD), ('value', c.c_double)]


class CounterItem(c.Structure):
    _fields_ = [('name', w.LPWSTR), ('value', CounterValue)]


class GpuMemory:
    def __init__(self):
        self.api = c.WinDLL('pdh')
        self.api.PdhOpenQueryW.argtypes = [w.LPCWSTR, c.c_size_t, c.POINTER(c.c_void_p)]
        self.api.PdhAddEnglishCounterW.argtypes = [c.c_void_p, w.LPCWSTR, c.c_size_t, c.POINTER(c.c_void_p)]
        self.api.PdhCollectQueryData.argtypes = [c.c_void_p]
        self.api.PdhGetFormattedCounterArrayW.argtypes = [c.c_void_p,w.DWORD,c.POINTER(w.DWORD),c.POINTER(w.DWORD),c.c_void_p]
        self.api.PdhCloseQuery.argtypes = [c.c_void_p]
        self.query = c.c_void_p()
        self.error = self.api.PdhOpenQueryW(None,0,c.byref(self.query))
        self.counters = {}
        if not self.error:
            for name in ['Shared Usage','Dedicated Usage']:
                counter = c.c_void_p()
                status = self.api.PdhAddEnglishCounterW(self.query,rf'\GPU Process Memory(*)\{name}',0,c.byref(counter))
                if status:
                    self.error = status
                else:
                    self.counters[name] = counter

    def read(self):
        if self.error:
            return {'error':self.error}
        status = self.api.PdhCollectQueryData(self.query)
        if status:
            return {'error':status}
        result = {}
        for name, counter in self.counters.items():
            size, count = w.DWORD(), w.DWORD()
            self.api.PdhGetFormattedCounterArrayW(counter,0x200,c.byref(size),c.byref(count),None)
            if not size.value:
                result[name] = None; continue
            buffer = c.create_string_buffer(size.value)
            status = self.api.PdhGetFormattedCounterArrayW(counter,0x200,c.byref(size),c.byref(count),buffer)
            if status:
                result[name] = None; continue
            items = c.cast(buffer,c.POINTER(CounterItem))
            values = [items[i].value.value for i in range(count.value)
                      if items[i].name.startswith(f'pid_{os.getpid()}_') and items[i].value.status in (0,1)]
            result[name] = int(sum(values)) if values else None
        return result

    def close(self):
        if self.query:
            self.api.PdhCloseQuery(self.query)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--raw',type=Path,required=True)
    parser.add_argument('--model',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    process=psutil.Process(); gpu=GpuMemory(); samples=[]
    def sample(label):
        info=process.memory_info()
        item=dict(label=label,rss=info.rss,private=info.private,peak_rss=info.peak_wset,gpu=gpu.read())
        samples.append(item);return item
    try:
        sample('baseline')
        with rawpy.imread(str(args.raw)) as raw: packed=pack(raw)
        options=ort.SessionOptions();options.enable_mem_pattern=False
        options.execution_mode=ort.ExecutionMode.ORT_SEQUENTIAL
        options.add_session_config_entry('session.disable_cpu_ep_fallback','1')
        session=ort.InferenceSession(str(args.model),options,providers=[('DmlExecutionProvider',{'device_id':0})])
        session.disable_fallback()
        class Measured:
            def run(self,*arguments):
                result=session.run(*arguments);sample('tile');return result
        measured=Measured(); runs=[]
        for index in range(3):
            start=time.perf_counter();output,times=tiled(measured,packed,False)
            if not np.isfinite(output).all():raise ValueError('nonfinite full image')
            del output;gc.collect()
            runs.append(dict(index=index+1,tiles=len(times),seconds=time.perf_counter()-start,
                             after_release=sample('run-released')))
        cancelled,_=tiled(measured,packed,False,stop_after=1)
        if cancelled is not None:raise ValueError('cancel leaked partial result')
        sample('cancelled')
        del measured,session,packed;gc.collect();sample('session-released')
        rss_growth=runs[-1]['after_release']['rss']-runs[0]['after_release']['rss']
        peaks={name:max([s['gpu'][name] for s in samples if isinstance(s['gpu'].get(name),int)],default=None)
               for name in ['Shared Usage','Dedicated Usage']}
        report=dict(raw=args.raw.name,provider='DirectML',onnxruntime=ort.__version__,runs=runs,
            memory_samples=samples,peak_gpu_bytes=peaks,retained_rss_growth_bytes=rss_growth,
            stable_under_256MiB=bool(rss_growth<256*1024**2),cancel_discards_partial=True,
            limitations=['Three runs only, not a long-duration leak qualification.',
                'GPU memory is sampled after each tile, not a hardware high-water mark.',
                'ORT allocator/driver reservations can remain after individual image releases.'])
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(json.dumps({k:v for k,v in report.items() if k!='memory_samples'}),flush=True)
        if not report['stable_under_256MiB']:raise SystemExit(1)
    finally:
        gpu.close()


if __name__=='__main__':main()
