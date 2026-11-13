"""Exercise recording and asynchronous batch actions through the production UI."""
import ctypes,json,os,tempfile
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(root/'build/windows-release'))]
os.environ['QT_QPA_PLATFORM']='offscreen';os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
dll=ctypes.CDLL(str(root/'build/windows-release/huelith_python_driver.dll'))
fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
def call(op,**kw):
    r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
with tempfile.TemporaryDirectory(dir=root/'build',ignore_cleanup_errors=True) as tmp:
    tmp=Path(tmp);call('init',settings=str(tmp));call('window')
    call('window_scene',width=64,height=50,layers=[dict(name='source',color='red')])
    call('window_action',text='开始录制动作')
    call('window_control',name='opacitySlider',value=50,drag=True)
    steps=call('action_recording_state')['steps'];assert any(s['op']=='opacity' for s in steps),steps
    call('window_action',text='撤销')
    assert call('action_recording_state')['steps'][-1]['op']=='stop'
    call('window_action',text='结束录制并编辑动作…',dialog=dict(accept=False))
    Image.new('RGB',(160,80),'red').save(tmp/'source.png')
    dataset=tmp/'dataset.json'
    dataset.write_text(json.dumps([dict(file='source.png',variables=dict(w=40)),dict(file='missing.png'),dict(file='source.png',variables=dict(w=70))]))
    action=json.dumps([dict(op='resize',width='$w',height=30)])
    def batch(folder,**extra):
        folder.mkdir();call('window_action',text='动作批处理…',dialog=dict(accept=True,actionSteps=action,batchDataset=str(dataset),batchOutput=str(folder),**extra))
    batch(tmp/'out')
    for i,w in [(1,40),(3,70)]:assert Image.open(tmp/f'out/source-{i}.png').size==(w,30)
    assert 'missing.png' in call('action_recording_state')['summary']
    # Keep the worker busy long enough to cancel while the UI processes events.
    Image.new('RGB',(3000,2000),'red').save(tmp/'source.png')
    batch(tmp/'cancel',batchCancel=True)
    assert not list((tmp/'cancel').iterdir()),'cancelled batch left an output or temporary file'
    call('shutdown')
print('recording, unsupported undo stop, dataset error continuation and in-flight cancellation passed')
