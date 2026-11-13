"""Bounded PS07–12 integration, using the production Qt targets offscreen."""
import ctypes,json,os,tempfile,time,subprocess,sys,traceback
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/PS07-12';OUT.mkdir(parents=True,exist_ok=True)
if '--pipeline-only' not in sys.argv:subprocess.run([sys.executable,str(ROOT/'tools/ps07_09_validate.py')],check=True)
temp_root=ROOT/'build/qt-temp';temp_root.mkdir(exist_ok=True)
os.environ['TMP']=os.environ['TEMP']=str(temp_root)
qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='offscreen'
lib=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
fn=lib.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
results=[]
def call(op,**args):
    value=json.loads(fn(json.dumps(dict(op=op,**args),ensure_ascii=False).encode()))
    if 'error' in value:raise RuntimeError(value)
    return value
def test(name,body):
    try:body();row=dict(name=name,passed=True)
    except Exception:row=dict(name=name,passed=False,error=traceback.format_exc())
    results.append(row);print(json.dumps(row,ensure_ascii=False),flush=True)
def wait(window=False):
    until=time.monotonic()+30
    while time.monotonic()<until:
        items=call('export_queue',window=window)['items']
        if items and all(x['status']>=3 for x in items):return items
        call('pump',waitMs=30)
    raise AssertionError(items)
with tempfile.TemporaryDirectory(prefix='ps07-12-',dir=ROOT/'build',ignore_cleanup_errors=True) as folder:
    call('init',settings=folder)
    try:
        def pipeline():
            tool=ROOT/'build/dependencies/c2patool/c2patool'
            result=call('document_pipeline_probe',directory=folder,cmykProfile=str(ROOT/'build/default_cmyk.icc'),c2pa=str(tool/'c2patool.exe'),signedSample=str(tool/'sample/C.jpg'))
            (OUT/'pipeline.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
            assert result['channelCounts']==[3,1,3],result
            assert abs(result['whiteLab'][0]-100)<.001 and max(abs(v) for v in result['whiteLab'][1:])<.02,result
            assert max(abs(v-1) for v in result['whiteBack'])<.0001,result
            for key in ['cmykRoundtrip','cmykTiff','assignChanges','convertPreserves','boardMatches','layoutRoundtrip','spotChanges','spotPreview','pdfDistinct','signedValid','signedPresent']:assert result[key],(key,result)
            assert result['sliceWidth']==32 and result['resizeSlice']==64 and result['pdfCount']==2 and not result['unsignedPresent'],result
        test('PS10–12 color channels ICC separation layout persistence PDF C2PA',pipeline)
        if '--pipeline-only' in sys.argv:
            if not results[0]['passed']:raise RuntimeError(results[0])
        call('window');call('window_scene',scene='hit')
        def exports():
            call('window_action',text='批量导出与队列…')
            call('export_queue',window=True,exportQueueDirectory=folder,exportQueueSizes='0.5x, 2x',exportQueueNaming='scale_{size}',click='exportQueueStart',capture=str(OUT/'export.png'))
            items=wait(True);assert len(items)==2 and all(x['status']==3 for x in items),items
            sizes=[Image.open(x['path']).size for x in items];assert sizes[1]==tuple(v*4 for v in sizes[0]),sizes
            for fmt,bits in [('jpg',8),('png',16),('tif',16)]:
                call('export_queue',reset=True,path=str(Path(folder)/f'output-{bits}.{fmt}'),format=fmt,bits=bits,start=True)
                assert wait()[0]['status']==3
            protected=Path(folder)/'protected.png';protected.write_bytes(b'KEEP')
            try:call('export_queue',reset=True,path=str(protected),protected=str(protected),conflict=2,start=True)
            except RuntimeError:pass
            else:assert wait()[0]['status']==4
            assert protected.read_bytes()==b'KEEP'
            call('export_queue',reset=True,path=str(Path(folder)/'missing/output.png'),start=True)
            assert wait()[0]['status']==4
        test('PS12 multipliers PNG16 TIFF16 JPEG output and source/failure protection',exports)
        def retouch():
            call('window_scene',layers=[dict(name='paint',color='white')])
            call('window_key',key='B');call('window_key',key='D')
            for name,value in [('brushHardness',100),('brushOpacity',100),('brushFlow',100)]:call('window_control',name=name,value=value)
            def tap(x,y,modifiers=0):
                for phase in ['press','release']:call('window_mouse',phase=phase,x=x,y=y,modifiers=modifiers)
            tap(100,100)
            assert call('window_probe',x=100,y=100)['rgba'][0]<10
            call('window_control',name='toolButton_16',value=True)
            tap(100,100,134217728);tap(180,100)
            assert call('window_probe',x=180,y=100)['rgba'][0]<10
            call('window_key',key='Ctrl+Z')
            assert call('window_probe',x=180,y=100)['rgba'][0]>245
            call('window_key',key='B')
            call('window_action',text='工具预设…',dialog=dict(accept=False,steps=[dict(control='toolPresetName',value='roundtrip'),dict(click='保存当前工具')],capture=str(OUT/'tool-preset.png')))
        test('PS07 clone samples current edits and undo leaves source intact',retouch)
        def text_edit():
            assert call('text_edit_probe',directory=str(OUT))['ime']==[True,True,True]
        test('PS09 horizontal vertical path IME preedit and commit',text_edit)
        def dialogs():
            for text,name in [('文档颜色与精度…','color'),('图像大小…','image-size'),('画布大小…','canvas-size'),('画板管理…','boards'),('切片管理…','slices'),('消失点 · 透视贴图…','perspective')]:
                call('window_action',text=text,dialog=dict(accept=False,capture=str(OUT/f'{name}.png')))
            call('window_action',text='文档颜色与精度…',dialog=dict(accept=True,documentColorMode=2,documentColorDepth=1))
            call('window_action',text='撤销')
            call('ps_state',width=1366,height=768,capture=str(OUT/'main-1366.png'))
        test('PS08–12 entry points cancel color apply undo and compact layout',dialogs)
    finally:call('shutdown')
(OUT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
if not all(x['passed'] for x in results):sys.exit(1)
