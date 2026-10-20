"""Shortcut search, atomic conflicts, factory reset and a fresh-process RAW/tone binding."""
import ctypes,json,os,subprocess,sys,tempfile,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/shortcuts'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    def bindings():return {a['text']:a['shortcut'] for a in call('window_interaction_state')['shortcuts']}
    def dialog(**kw):return call('window_action',text='设置菜单快捷键…',dialog=kw)
    if len(sys.argv)>1 and sys.argv[1]=='--child':
        call('init',settings=sys.argv[2]);call('window');call('window_new')
        try:assert bindings()['色相/饱和度…']=='Ctrl+Alt+U',bindings()
        finally:call('shutdown')
        return
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-shortcuts-') as folder:
        call('init',settings=folder);call('window');call('window_new')
        try:
            def search():
                assert dialog(shortcutSearch='自由变换')['shortcutVisibleRows']==1
                assert dialog(shortcutSearch='Ctrl+C')['shortcutVisibleRows']==1
                assert dialog(shortcutSearch='this command does not exist')['shortcutVisibleRows']==0
                assert dialog(shortcutSearch='',capture=str(OUT/'all-commands.png'))['shortcutVisibleRows']>40
            test('command_and_key_search_clear_no_match_and_all_commands',search)
            def conflict():
                before=bindings();r=dialog(conflict=True)
                assert r['conflictDetected'] and bindings()==before
                dialog(binding=dict(name='色相/饱和度…',key='Ctrl+Alt+U'),accept=False)
                assert bindings()==before
            test('conflict_rejects_atomically_and_cancel_keeps_bindings',conflict)
            def persistence():
                dialog(binding=dict(name='色相/饱和度…',key='Ctrl+Alt+U'),accept=True)
                assert bindings()['色相/饱和度…']=='Ctrl+Alt+U'
                child=subprocess.run([sys.executable,__file__,'--child',folder],capture_output=True,text=True,timeout=40)
                assert child.returncode==0,child.stdout+child.stderr
                dialog(restoreDefaults=True,accept=True)
                assert bindings()['色相/饱和度…']=='Ctrl+U'
            test('late_registered_tone_command_persists_new_process_and_factory_reset',persistence)
            def same_labels():
                dialog(binding=dict(name='视图 / 吸附到 / 网格',key='Ctrl+Alt+Shift+F11'),accept=True)
                paths={a['path']:a['shortcut'] for a in call('window_chrome')['menus']}
                snap=[v for k,v in paths.items() if k.endswith('/吸附到/网格')]
                show=[v for k,v in paths.items() if k.endswith('/显示/网格')]
                assert snap==['Ctrl+Alt+Shift+F11'] and show==["Ctrl+'"],(snap,show)
                before=call('canvas_preferences_state')['snapGrid']
                call('window_key',key='Ctrl+Alt+Shift+F11')
                assert call('canvas_preferences_state')['snapGrid'] != before
                dialog(restoreDefaults=True,accept=True)
                paths={a['path']:a['shortcut'] for a in call('window_chrome')['menus']}
                assert [v for k,v in paths.items() if k.endswith('/吸附到/网格')]==['']
                assert [v for k,v in paths.items() if k.endswith('/显示/网格')]==["Ctrl+'"]
            test('same_label_commands_have_independent_binding_identity_and_reset',same_labels)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
