"""RAW denoise action/status regression using the existing production Qt DLL bridge."""
import ctypes, json, os, tempfile, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/raw-denoise-action'
OUT.mkdir(parents=True,exist_ok=True)
qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
fn=dll.huelith_python_command
fn.argtypes,fn.restype=[ctypes.c_char_p],ctypes.c_char_p
def call(op='raw_window_state',**kw):
 r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert not r.get('error'),r;return r
def wait(predicate):
 end=time.monotonic()+180
 while time.monotonic()<end:
  call('pump',waitMs=30);s=call()
  if predicate(s):return s
 raise TimeoutError(s)
with tempfile.TemporaryDirectory() as settings:
 call('init',settings=settings);call('window')
 call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),model=str(ROOT/'research/m0/model-package/rawdenoise-nind/model_bayer.onnx'),denoise=False)
 s=wait(lambda s:s['ready'] and s['previewPresented']>0)
 assert s['denoiseButton']=='开始降噪' and s['denoiseInSection'] and not s['progressInSection']
 assert not s['denoiseEnabled'] and not s['progressVisible']
 prev=s['previewPresented'];call(exposure=.3)
 wait(lambda s:s['previewPresented']>prev and s['ready'])
 s=call(denoise=True,section=7)
 assert s['idle'] and s['denoiseEnabled'] and not s['ready']
 call(click='rawCompute');s=call()
 assert not s['idle'] and s['progressVisible']
 call(widget='rawCompute');call(capture=str(OUT/'running.png'))
 call(click='rawCancelCompute');s=wait(lambda s:s['idle'])
 assert s['visible'] and not s['progressVisible']
 call(denoise=False);wait(lambda s:s['ready'])
 call(denoise=True);call(click='rawCompute')
 s=wait(lambda s:s['ready'] and s['idle'])
 assert s['recipe']['denoise'] and not s['progressVisible']
 prev=s['previewPresented'];call(control='rawDenoiseAmount',value=25)
 wait(lambda s:s['previewPresented']>prev and s['ready'])
 call(widget='rawCompute');call(capture=str(OUT/'completed.png'))
 call(denoise=False);s=wait(lambda s:s['ready'])
 assert not s['denoiseEnabled']
 call(reject=True);wait(lambda s:not s['visible'])
 (OUT/'results.json').write_text(json.dumps(dict(passed=True,checks=['action inside AI RAW section','fixed footer status','automatic exposure preview','explicit denoise execution','cancel retains window','restart real RAW model','amount automatic preview','disable automatic restoration']),ensure_ascii=False,indent=2),encoding='utf-8')
 print('PASS RAW denoise action, cancellation, restart, automatic preview',flush=True)
