from pathlib import Path
import json,sys,tempfile,os,ctypes
from PIL import Image
root=Path(__file__).resolve().parents[1]
qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(root/'build/windows-release'))]
os.environ['QT_QPA_PLATFORM']='offscreen';os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
dll=ctypes.CDLL(str(root/'build/windows-release/huelith_python_driver.dll'))
f=dll.huelith_python_command;f.argtypes=[ctypes.c_char_p];f.restype=ctypes.c_char_p
def call(op,**kw):
 r=json.loads(f(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import threading,io
graph=json.loads((root/'apps/desktop/assets/workflows/inpaint.json').read_text())
schema={n['class_type']:{'input':{'required':{}}} for n in graph.values()}
schema['CheckpointLoaderSimple']['input']['required']['ckpt_name']=[['test.safetensors']]
seen=[];png=io.BytesIO();Image.new('RGB',(64,56),'green').save(png,format='PNG')
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*a): pass
 def send(self,data,kind='application/json'):
  if not isinstance(data,bytes):data=json.dumps(data).encode()
  self.send_response(200);self.send_header('Content-type',kind);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def do_POST(self):
  data=self.rfile.read(int(self.headers['Content-Length']));seen.append(self.path)
  if self.path=='/upload/image':self.send(dict(name='upload.png',subfolder='test'));return
  assert self.path=='/prompt';g=json.loads(data)['prompt'];assert g['2']['inputs']['image']=='test/upload.png' and g['1']['inputs']['ckpt_name']=='test.safetensors';self.send(dict(prompt_id='test-id'))
 def do_GET(self):
  seen.append(self.path)
  if self.path.startswith('/ws'):self.send_error(404)
  elif self.path=='/object_info':self.send(schema)
  elif self.path.startswith('/history/'):self.send({'test-id':{'outputs':{'9':{'images':[dict(filename='final.png',subfolder='',type='output')]},'12':{'images':[dict(filename='intermediate.png')]}}}})
  else:assert 'final.png' in self.path;self.send(png.getvalue(),'image/png')
server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
with tempfile.TemporaryDirectory(dir=root/'build',ignore_cleanup_errors=True) as tmp:
 call('init',settings=tmp);call('window')
 budget=call('pixel_storage_budget_probe',folder=tmp);assert all(budget.values()),budget
 call('window_scene',width=64,height=50,layers=[dict(name='底',color='red'),dict(name='顶',color='blue')])
 for ext in ['psd','psb']:
  path=Path(tmp)/('layers.'+ext);result=call('photoshop_roundtrip',path=str(path))
  assert result['names']==['底','顶'] and result['samePixels'] and not result['warnings'],result
  if ext=='psd':
   image=Image.open(path);rgb=image.convert('RGB').getpixel((0,0))
   assert image.size==(64,50) and len(image.layers)==2 and max(abs(a-b) for a,b in zip(rgb,(0,0,255)))<=1,rgb
 call('window_open',path=str(path))
 call('window_select',index=0)
 call('window_action',text='编辑与播放动作…',dialog=dict(accept=True,actionSteps=json.dumps([dict(op='rename',value='输入层'),dict(op='resize',width=64,height=50)])))
 assert any(l['name']=='输入层' for l in call('window_probe',x=2,y=2)['layers'])
 batch=Path(tmp)/'batch';batch.mkdir();Image.new('RGB',(40,20),'red').save(batch/'sample.png')
 ds=batch/'dataset.json';ds.write_text(json.dumps([{'file':'sample.png','variables':{'w':20}},{'file':'missing.png','variables':{'w':20}},{'file':'sample.png','variables':{'w':30}}]),encoding='utf-8')
 call('window_action',text='动作批处理…',dialog=dict(accept=True,actionSteps=json.dumps([dict(op='resize',width='$w',height=10,**{'if':'landscape'})]),batchDataset=str(ds),batchOutput=str(batch)))
 for suffix,width in [(1,20),(3,30)]:
  with Image.open(batch/f'sample-{suffix}.png') as result:assert result.size==(width,10)
 assert not (batch/'missing-2.png').exists()
 call('window_action',text='生成工作流（ComfyUI）…')
 def control(k,v=0):call('window_control',name=k,value=v)
 control('comfyTemplates',1);control('comfyAddress',f'http://127.0.0.1:{server.server_port}');control('comfyCheck')
 for _ in range(20):call('pump',waitMs=20)
 control('comfyParam_1_ckpt_name',1);control('comfySource_primary',2);control('comfySource_editMask',1);control('comfyGenerate')
 for _ in range(200):
  call('pump',waitMs=15);r=call('window_probe',x=10,y=10)
  if len(r['layers'])==3:break
 assert len(r['layers'])==3 and seen.count('/upload/image')==2,(r,seen)
 assert r['rgba'][1]>=125 and r['rgba'][2]<5,r
 call('window_key',key='Ctrl+Z');r=call('window_probe',x=10,y=10)
 assert len(r['layers'])==2 and r['rgba'][2]>245,r
 call('window_action',text='重做')
 path=str(Path(tmp)/'result.huelith');call('window_design',file=path);call('window_open',path=path)
 assert len(call('window_probe',x=10,y=10)['layers'])==3
 call('window_action',text='帮助与诊断…',dialog=dict(accept=False,capture=str(Path(tmp)/'help.png')))
 assert Image.open(Path(tmp)/'help.png').width>300
 print('PSD/PSB roundtrip, action playback, two uploads, final result filtering, undo, save/reopen and help dialog passed')
 call('shutdown');server.shutdown()
