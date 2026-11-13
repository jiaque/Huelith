"""Deterministic HTTP/WS protocol checks. No model inference is claimed."""
from pathlib import Path
import base64,ctypes,hashlib,io,json,os,struct,tempfile,threading,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from PIL import Image
root=Path(__file__).resolve().parents[1];qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(root/'build/windows-release'))]
os.environ['QT_QPA_PLATFORM']='offscreen';os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
dll=ctypes.CDLL(str(root/'build/windows-release/huelith_python_driver.dll'))
fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
def call(op,**kw):
    r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
graph=json.loads((root/'apps/desktop/assets/workflows/inpaint.json').read_text(encoding='utf-8'))
manifest=json.loads((root/'apps/desktop/assets/workflows/manifest.json').read_text(encoding='utf-8'))
graph['10']={'class_type':'LoadImageMask','inputs':{'image':'reference.png','channel':'red'}}
manifest['inputs'].append(dict(node='10',input='image',role='referenceMask',kind='mask',whiteMeans='edit'))
manifest['workflow']='workflow.json'
schema={n['class_type']:{'input':{'required':{}}} for n in graph.values()}
schema['CheckpointLoaderSimple']['input']['required']['ckpt_name']=[['test.safetensors']]
schema['KSampler']['input']['required']['denoise']=['FLOAT',dict(min=0,max=1,step=.01)]
schema['LoadImageMask']['input']['optional']={'strength':['FLOAT',dict(min=0,max=1,default=.5)]}
submitted=threading.Event();seen=[];png=io.BytesIO();Image.new('RGB',(64,56),'white').save(png,format='PNG')
started=0
class Handler(BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args):pass
    def send(self,data,kind='application/json'):
        if not isinstance(data,bytes):data=json.dumps(data).encode()
        self.send_response(200);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def do_POST(self):
        global started
        body=self.rfile.read(int(self.headers['Content-Length']));seen.append(self.path)
        if self.path=='/upload/image':self.send(dict(name='input.png'));return
        assert self.path=='/prompt'
        data=json.loads(body)['prompt'];assert data['10']['inputs']['strength']==.5,data
        started=time.monotonic();submitted.set();self.send(dict(prompt_id='own-job'))
    def do_GET(self):
        if self.path.startswith('/ws?'):
            key=self.headers['Sec-WebSocket-Key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
            self.send_response(101);self.send_header('Upgrade','websocket');self.send_header('Connection','Upgrade')
            self.send_header('Sec-WebSocket-Accept',base64.b64encode(hashlib.sha1(key.encode()).digest()).decode());self.end_headers()
            submitted.wait(5);time.sleep(.25)
            for prompt,node in [('other-job','WRONG'),('own-job','7')]:
                data=json.dumps(dict(type='progress',data=dict(prompt_id=prompt,node=node,value=3,max=8))).encode()
                frame=bytes([0x81,len(data)]) if len(data)<126 else b'\x81\x7e'+struct.pack('>H',len(data))
                self.wfile.write(frame+data);self.wfile.flush();time.sleep(.1)
            time.sleep(3);self.close_connection=True;return
        if self.path=='/object_info':self.send(schema)
        elif self.path.startswith('/history/'):
            self.send({} if time.monotonic()-started<2 else {'own-job':dict(outputs={'9':dict(images=[dict(filename='final.png',type='output',subfolder='')])})})
        else:self.send(png.getvalue(),'image/png')
server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
with tempfile.TemporaryDirectory(dir=root/'build',ignore_cleanup_errors=True) as tmp:
    tmp=Path(tmp);(tmp/'workflow.json').write_text(json.dumps(graph));(tmp/'manifest.json').write_text(json.dumps(manifest))
    call('init',settings=str(tmp));call('window');call('window_scene',width=64,height=50,layers=[dict(name='source',color='black')])
    call('comfy_probe',coverage=128);call('window_action',text='生成工作流（ComfyUI）…')
    def control(name,value=0,**kw):call('window_control',name=name,value=value,**kw)
    control('comfyImport',path=str(tmp/'manifest.json'));print('import',call('comfy_probe'),flush=True);control('comfyAddress',f'http://127.0.0.1:{server.server_port}');control('comfyCheck')
    for _ in range(20):call('pump',waitMs=20)
    print('dependency status:',call('comfy_probe')['status'],flush=True)
    control('comfyOptional_10_strength',True)
    for _ in range(4):call('pump',waitMs=20)
    control('comfyParam_1_ckpt_name',1);control('comfySource_primary',2);control('comfySource_referenceMask',1);control('comfyGenerate')
    progress=False
    for _ in range(300):
        call('pump',waitMs=20);status=call('comfy_probe')['status'];assert 'WRONG' not in status,status
        progress |= '3 / 8' in status
        result=call('window_probe',x=10,y=10)
        if len(result['layers'])==2:break
    assert len(result['layers'])==2,(result,status)
    assert 186<=result['rgba'][0]<=189,result # Linear-light 50% white over black.
    assert progress,'no own-job WS progress'
    assert seen.count('/upload/image')==3,seen
    probe=call('comfy_probe');assert len(probe['inputs'])==5,probe
    project=str(tmp/'generated.huelith');call('window_design',file=project);call('window_open',path=project)
    after=call('comfy_probe');assert after['inputs']==probe['inputs'] and after['record']==probe['record']
    call('shutdown')
server.shutdown()
print('multiple masks, optional parameter, own-job WebSocket progress, linear blend and frozen inputs save/reopen passed')
