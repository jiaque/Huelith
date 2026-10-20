"""Production floating documents, shared editing and persisted whole-interface scaling."""
import ctypes,json,os,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/workspace-closeout'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    if len(sys.argv)>1:
        call('init',settings=sys.argv[1]);call('window');call('window_new')
        r=call('window_workspace');call('window_design',capture=str(OUT/f"scale-{r['scalePreference']}.png"))
        print(json.dumps(r));call('shutdown');return
    records=[]
    with tempfile.TemporaryDirectory(prefix='huelith-workspace-final-') as folder:
        call('init',settings=folder);call('window');call('window_new',width=600,height=400)
        try:
            call('window_key',key='U');call('window_gesture',x=80,y=80,endX=230,endY=220)
            before=call('window_design');ident=before['layers'][-1]['id'];history=before['history']
            call('window_action',text='在窗口中浮动当前文档')
            r=call('window_workspace');assert r['floating'] and r['documentCount']==1,r
            call('window_key',key='Ctrl+Z');assert call('window_design')['history']==history-1
            call('window_key',key='Ctrl+Shift+Z');assert call('window_design')['layers'][-1]['id']==ident
            call('window_control',name='shapeRadius',value=20)
            assert call('window_design')['layers'][-1]['shape']['radius']==20
            call('window_new',width=300,height=200);assert not call('window_workspace')['floating']
            call('window_workspace',activateDocument=0)
            assert call('window_workspace')['floating'] and call('window_design')['layers'][-1]['id']==ident
            call('window_action',text='将所有文档合并到标签栏')
            assert not call('window_workspace')['floating']
            assert call('window_design')['layers'][-1]['id']==ident
            call('window_action',text='在窗口中浮动当前文档')
            call('window_action',text='关闭图片',answer='cancel')
            assert call('window_workspace')['documentCount']==2
            call('window_action',text='关闭图片',answer='discard')
            assert call('window_workspace')['documentCount']==1
            records.append(dict(case='float edit undo redo switch dock close cancel discard',passed=True))
            # Each child process runs the same startup scale code as the packaged application.
            values=[]
            env=dict(os.environ);env.pop('QT_SCALE_FACTOR',None)
            for index,scale in enumerate([.75,1,1.25,1.5,2]):
                call('window_action',text='界面与工作区…',dialog=dict(accept=True,interfaceScalePreference=index))
                child=subprocess.run([sys.executable,__file__,folder],env=env,capture_output=True,text=True,encoding='utf-8')
                assert child.returncode==0,(child.stdout,child.stderr)
                r=json.loads(child.stdout.strip().splitlines()[-1]);assert r['scalePreference']==index,r
                values.append(r['dpr']/scale)
            assert max(values)-min(values)<1e-6,values
            call('window_action',text='界面与工作区…',dialog=dict(accept=False,interfaceScalePreference=0))
            assert call('window_workspace')['scalePreference']==4
            call('window_action',text='界面与工作区…',dialog=dict(accept=True,interfaceScalePreference=1))
            records.append(dict(case='five startup scales actual device pixels persist cancel',passed=True,baseDpr=values[0]))
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    print(records,flush=True)

if __name__=='__main__':main()
