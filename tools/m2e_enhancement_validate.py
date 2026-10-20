"""Drive production enhancement and its real worker from the established Python DLL bridge."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import time
import threading
from ctypes import wintypes
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/test-evidence/M2-E/enhancement'

def kill_owned_worker():
    class Entry(ctypes.Structure):
        _fields_=[('size',wintypes.DWORD),('usage',wintypes.DWORD),('pid',wintypes.DWORD),
                  ('heap',ctypes.c_size_t),('module',wintypes.DWORD),('threads',wintypes.DWORD),
                  ('parent',wintypes.DWORD),('priority',wintypes.LONG),('flags',wintypes.DWORD),
                  ('name',wintypes.WCHAR*260)]
    api=ctypes.WinDLL('kernel32',use_last_error=True)
    api.CreateToolhelp32Snapshot.restype=wintypes.HANDLE
    api.Process32FirstW.argtypes=[wintypes.HANDLE,ctypes.POINTER(Entry)]
    api.Process32NextW.argtypes=[wintypes.HANDLE,ctypes.POINTER(Entry)]
    api.OpenProcess.restype=wintypes.HANDLE
    api.TerminateProcess.argtypes=[wintypes.HANDLE,wintypes.UINT]
    api.CloseHandle.argtypes=[wintypes.HANDLE]
    snapshot=api.CreateToolhelp32Snapshot(2,0)
    entry=Entry();entry.size=ctypes.sizeof(entry)
    try:
        valid=api.Process32FirstW(snapshot,ctypes.byref(entry))
        while valid:
            if entry.parent==os.getpid() and entry.name.lower()=='huelith.exe':
                handle=api.OpenProcess(1,False,entry.pid)
                if handle:
                    try:
                        if api.TerminateProcess(handle,77):return entry.pid
                    finally:api.CloseHandle(handle)
            valid=api.Process32NextW(snapshot,ctypes.byref(entry))
    finally:api.CloseHandle(snapshot)
    return None

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    # QProcess children need a real loader path; add_dll_directory only affects this process.
    os.environ['PATH'] = str(qt/'bin') + os.pathsep + str(ROOT/'build/windows-release') + os.pathsep + os.environ['PATH']
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    dll = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = dll.huelith_python_command
    fn.argtypes = [ctypes.c_char_p]; fn.restype = ctypes.c_char_p
    def raw(op, **kw): return json.loads(fn(json.dumps(dict(op=op, **kw)).encode()))
    def call(op, **kw):
        result = raw(op, **kw)
        assert 'error' not in result, result
        return result
    records = []
    with tempfile.TemporaryDirectory(prefix='huelith-enhance-test-') as folder:
        call('init', settings=folder)
        try:
            image = Image.open(ROOT/'research/m2/semantic/samples/ADE_val_00000001.jpg').convert('RGB').resize((80,64))
            source = OUT/'source.png'; image.save(source)
            for scale in (2,4):
                result = call('enhancement_probe', source=str(source), scale=scale, output=str(OUT/f'super-{scale}.png'))
                assert (result['width'],result['height']) == (80*scale,64*scale), result
                assert result['workerIsolated'] and result['progressEvents'] > 0, result
                records.append(dict(case=f'super-{scale}', **result)); print(records[-1],flush=True)
            result=call('enhancement_probe',source=str(source),scale=2,quality=True,output=str(OUT/'super-quality-2.png'))
            assert result['model']=='realesrgan-x4plus.onnx' and result['width']==160,result
            records.append(dict(case='super-quality-real-model',**result));print(records[-1],flush=True)
            # Real noise removal should improve the known flat-field reference, not merely change pixels.
            clean = np.full((96,96,3), 128.,dtype=np.float32)
            noise = np.clip(clean+np.random.default_rng(724).normal(0,16,clean.shape),0,255).astype(np.uint8)
            noisy = OUT/'noise.png'; Image.fromarray(noise).save(noisy)
            result = call('enhancement_probe', mode=1, source=str(noisy), output=str(OUT/'denoised.png'))
            restored = np.array(Image.open(OUT/'denoised.png')).astype(float)[...,:3]
            before = float(np.mean((noise.astype(float)-clean)**2)); after = float(np.mean((restored-clean)**2))
            assert after < before*.6, (before,after,result)
            records.append(dict(case='denoise',mseBefore=before,mseAfter=after,**result)); print(records[-1],flush=True)
            result = call('enhancement_probe', mode=2, source=str(source), output=str(OUT/'depth.png'))
            assert (result['width'],result['height']) == (80,64) and result['relativeDepth'], result
            assert np.ptp(np.array(Image.open(OUT/'depth.png'))) > 100
            records.append(dict(case='relative-depth',**result)); print(records[-1],flush=True)
            rgba = np.empty((33,35,4),np.uint8); rgba[...,:3]=(120,80,30); rgba[...,3]=np.arange(35)*7
            transparent = OUT/'alpha.png'; Image.fromarray(rgba).save(transparent)
            result = call('enhancement_probe',source=str(transparent),scale=2,output=str(OUT/'alpha-2.png'))
            out = np.array(Image.open(OUT/'alpha-2.png'))
            assert out.shape==(66,70,4) and out[0,0,3]==0 and abs(int(out[-1,-1,3])-238)<=1
            start=time.perf_counter(); cancelled=raw('enhancement_probe',source=str(source),quality=True,cancelAfterMs=150)
            assert '取消' in cancelled.get('error','') and time.perf_counter()-start < 4, cancelled
            missing=raw('enhancement_probe',source=str(source),model=str(OUT/'missing.onnx'))
            assert 'error' in missing, missing
            # The parent remains usable after cancellation and worker failure.
            call('enhancement_probe',mode=1,strength=0,source=str(source))
            records.append(dict(case='alpha-cancel-worker-error-parent-survives',passed=True));print(records[-1],flush=True)
            # Kill only the real worker spawned by this test process, then require CPU recovery.
            stop=threading.Event(); killed=[]
            def kill_worker_once():
                while not stop.wait(.01):
                    pid=kill_owned_worker()
                    if pid: killed.append(pid);return
            killer=threading.Thread(target=kill_worker_once);killer.start()
            try: result=call('enhancement_probe',source=str(source),directml=True)
            finally: stop.set();killer.join()
            assert killed and result['cpuFallback'] and result['provider']=='CPU',result
            records.append(dict(case='real-worker-kill-cpu-retry',**result));print(records[-1],flush=True)
            result=call('enhancement_probe',source=str(source),directml=True)
            records.append(dict(case='available-gpu-or-reported-fallback',**result));print(records[-1],flush=True)
            call('window')
            call('window_open',path=str(source))
            for mode in (0,1,2):
                result=call('enhancement_ui',mode=mode,capture=str(OUT/f'dialog-{mode}.png'))
                assert result['previewed'],result
                if mode<2: assert result['documentsAfter']==result['documentsBefore']+1 and result['dirty'],result
                else: assert result['hasSelection'] and result['documentsAfter']==result['documentsBefore'],result
                records.append(dict(case=f'actual-menu-preview-apply-{mode}',**result));print(records[-1],flush=True)
            result=call('enhancement_ui',cancel=True)
            assert result['previewed'] and result['documentsAfter']==result['documentsBefore'],result
            records.append(dict(case='menu-preview-cancel-no-document-created',**result))
            result=call('enhancement_ui',mode=2,changeSource=True)
            assert result['hasSelection'] and result['documentsAfter']==result['documentsBefore']+1,result
            records.append(dict(case='concurrent-source-edit-preserves-depth-result-as-copy',**result))
        finally:
            call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')

if __name__=='__main__': main()
