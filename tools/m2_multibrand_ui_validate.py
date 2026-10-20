"""Independent rawpy/LibRaw reference and actual RAW window capability/recipe checks."""
import ctypes,json,os,sys,time,tempfile,shutil
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/multibrand';qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))];os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
def call(op='raw_window_state',**kw):
 r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert not r.get('error'),r;return r
def wait(pred):
 end=time.monotonic()+120
 while time.monotonic()<end:
  call('pump',waitMs=30);s=call()
  if pred(s):return s
 raise TimeoutError(s)
source=Path(sys.argv[1]).resolve();key=source.stem;report={}
with tempfile.TemporaryDirectory() as settings:
 call('init',settings=settings)
 try:
  cap=call('raw_capabilities',path=str(source));assert cap['recognized'];report['capabilities']=cap
  renamed=Path(settings)/'camera.unknownraw';shutil.copyfile(source,renamed);assert call('raw_capabilities',path=str(renamed))['recognized'];report['contentRecognition']=True
  linked=Path(settings)/source.name;shutil.copyfile(source,linked)
  call('window');call('raw_window',path=str(linked),denoise=not cap['rawDenoiseSupported'],linked=True)
  s=wait(lambda s:s['ready'] and s['previewPresented']>0)
  assert s['denoiseSupported']==cap['rawDenoiseSupported'] and s['dngSupported']==cap['rawDngSupported']
  if not cap['rawDenoiseSupported']:assert not s['denoiseToggleEnabled'] and s['capabilityNote'] and not s['recipe']['denoise']
  prev=s['previewPresented'];call(exposure=.7);s=wait(lambda s:s['previewPresented']>prev)
  assert s['recipe']['tone']['exposure']==.7
  prev=s['previewPresented'];call(control='rawWhiteBalanceTemperature',value=5800);s=wait(lambda s:s['previewPresented']>prev)
  report['whiteBalance']=s['recipe'].get('whiteBalance');report['uiReady']=s['ready'];assert s['ready'];print('preview ready',source.name,flush=True)
  call(section=7);call(widget='rawCompute');call(capture=str(OUT/(key+'-window.png')))
  if cap['rawDngSupported']:
   saved=Path(settings)/'ui-copy.dng';r=call('dng_ui',output=str(saved));assert r['saved'] and r['open'],(r,call()['status']);report['uiDng']=r
  print('before accept',call()['status'],flush=True);call(accept=True,asyncAccept=True);s=wait(lambda s:not s['visible']);assert s['result']==1
  call(save=str(OUT/(key+'-edited.huelith')));report['projectSaved']=True
  assert Path(str(linked)+'.hraw.json').exists();report['sidecarSaved']=True
  # Process the source through the product, compare with separately distributed rawpy/LibRaw.
  rawfile=OUT/(key+'-reference.float');r=call('raw_develop',path=str(source),floatOutput=str(rawfile));meta=r['metadata']
  if meta['backend']=='libraw-0.22-linear16':
   sys.path.insert(0,str(ROOT/'build/raw-reference-python'));import rawpy
   with rawpy.imread(str(source)) as raw:
    camera=meta['whiteBalanceBasis']=='camera'
    kw=dict(use_camera_wb=not camera,no_auto_bright=True,output_bps=16,gamma=(1,1),output_color=rawpy.ColorSpace.raw if camera else rawpy.ColorSpace.sRGB,user_flip=0,adjust_maximum_thr=0,demosaic_algorithm=rawpy.DemosaicAlgorithm.AHD)
    if camera:kw['user_wb']=[1,1,1,1]
    else:
     gains=raw.camera_whitebalance
     if any(v<=0 for v in gains[:3]):kw['user_wb']=raw.daylight_whitebalance
    reference=raw.postprocess(**kw).astype(np.float32)/65535
    flip=raw.sizes.flip
   matrix=np.asarray(meta['cameraToWorking']).reshape(3,3);reference=reference@matrix.T
   if flip in (3,5,6):reference=np.rot90(reference,{3:2,5:1,6:3}[flip])
   actual=np.memmap(rawfile,dtype=np.float32,mode='r',shape=(r['height'],r['width'],4))
   assert reference.shape==actual[:,:,:3].shape
   error=np.abs(actual[:,:,:3]-reference);report['referenceMeanError']=float(error.mean());report['referenceMaxError']=float(error.max());report['referenceLibRaw']=rawpy.libraw_version
   assert error.max()<2e-3,report
   del actual
  rawfile.unlink();report['passed']=True
  (OUT/(key+'-ui.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(report,ensure_ascii=True),flush=True)
 finally:call('shutdown')
