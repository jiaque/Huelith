"""Production DLL multi-brand RAW matrix; each capability has a separate outcome."""
import ctypes,json,os,sys,time,hashlib
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/multibrand';OUT.mkdir(parents=True,exist_ok=True)
qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64');handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))];os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
lib=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=lib.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
def call(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
source=Path(sys.argv[1]).resolve();key=source.stem;result={'source':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
call('init',settings=str(OUT/'settings'))
result['capabilities']=call('raw_capabilities',path=str(source))
try:
 for mode in ['plain','dng','ai']:
  t=time.monotonic()
  if mode=='plain':
   r=call('raw_develop',path=str(source),floatOutput=str(OUT/(key+'.float')))
   if 'error' not in r:
    pix=np.memmap(OUT/(key+'.float'),dtype=np.float32,mode='r',shape=(r['height'],r['width'],4))
    assert np.isfinite(pix).all() and np.min(pix[:,:,3])==1 and np.std(pix[:,:,:3])>0
    r['finite']=True;r['std']=float(np.std(pix[:,:,:3]));r['range']=[float(pix[:,:,:3].min()),float(pix[:,:,:3].max())]
    # Rec2020 linear -> sRGB for a bounded contact sheet, using independent matrix.
    rgb=np.array(pix[::max(1,r['height']//450),::max(1,r['width']//650),:3]);rgb=rgb@np.array([[1.660491,-.587641,-.07285],[-.12455,1.1329,-.00835],[-.018151,-.100579,1.11873]]).T
    rgb=np.clip(rgb,0,1);rgb=np.where(rgb<=.0031308,12.92*rgb,1.055*rgb**(1/2.4)-.055);Image.fromarray((rgb*255).astype('uint8')).save(OUT/(key+'.jpg'))
    del pix; (OUT/(key+'.float')).unlink()
  elif mode=='dng':
   r=call('dng_probe',source=str(source),output=str(OUT/(key+'.dng')),recipe={'denoise':False,'tone':{'version':8,'exposure':.3}},compareDevelop=True)
   if 'error' not in r:
    assert r['before']['sensorSha256']==r['after']['sensorSha256'] and r['originalUnchanged'] and r['sameSize'],r
    r['sensorPreserved']=True
    assert r['maxError']<1e-4,r
  else:
   r=call('raw_develop',path=str(source),denoise=True,directml=True,model=str(ROOT/'build/windows-release/models/model_bayer.onnx'))
  r['seconds']=round(time.monotonic()-t,3);result[mode]=r
  (OUT/(key+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(key,mode,str(r.get('error',r.get('metadata',{}).get('backend','OK'))),r['seconds'],flush=True)
 assert 'error' not in result['plain'],result['plain']
 for mode,field in [('dng','rawDngSupported'),('ai','rawDenoiseSupported')]:
  expected=result['capabilities'][field]
  assert ('error' not in result[mode])==expected,(mode,result[mode],result['capabilities'])
 result['passed']=True
 (OUT/(key+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
finally:call('shutdown')
