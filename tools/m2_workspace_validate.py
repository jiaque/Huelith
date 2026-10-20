"""Real Qt UI font/tab/dock preferences and named workspace persistence."""
import ctypes,json,os,subprocess,sys,tempfile,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/workspace'
def main():
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    lib=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=lib.huelith_python_command
    fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
        if 'error' in r:raise RuntimeError(r['error'])
        return r
    def state(**kw):return call('window_workspace',**kw)
    def pref(accept=True,**kw):call('window_action',text='界面与工作区…',dialog=dict(accept=accept,**kw))
    def action(text,**kw):return call('window_action',text=text,dialog=kw)
    if len(sys.argv)>1:
        call('init',settings=sys.argv[1]);call('window');call('window_scene',width=400,height=300,layers=[])
        print(json.dumps(state(),ensure_ascii=False));call('shutdown');return 0
    OUT.mkdir(parents=True,exist_ok=True);records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(json.dumps(records[-1],ensure_ascii=False),flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-workspace-') as folder:
        call('init',settings=folder);call('window');call('window_scene',width=600,height=400,layers=[])
        revision=call('window_interaction_state')['revision']
        def fonts():
            heights=[]
            for index,size in enumerate([10,11,13,15]):
                pref(interfaceFontPreference=index);s=state();assert s['fontPixels']==size,s;heights.append(s['fontHeight'])
            assert heights==sorted(heights) and heights[0]<heights[-1],heights
            pref(interfaceFontPreference=0,accept=False);assert state()['fontPixels']==15
            pref(interfaceFontPreference=2)
        test('four_actual_menu_font_sizes_cancel_no_document_mutation',fonts)
        def tabs():
            before=state()['tabHeight']
            pref(movableTabsPreference=False,closeTabsPreference=False,largeTabsPreference=True)
            s=state();assert not s['movableTabs'] and not s['closeTabs'] and s['tabHeight']>before,s
            pref(movableTabsPreference=True,closeTabsPreference=True,largeTabsPreference=False,accept=False)
            assert state()==s
            pref(movableTabsPreference=True,closeTabsPreference=True,largeTabsPreference=False)
            assert state()['tabHeight']==before
        test('real_document_tab_size_move_close_and_cancel',tabs)
        def locked():
            pref(lockWorkspacePreference=True)
            assert all(not d['movable'] and not d['floatable'] for d in state()['docks'].values())
            call('window_action',text='锁定工作区')
            assert all(d['movable'] and d['floatable'] for d in state()['docks'].values())
            pref(lockWorkspacePreference=True,accept=False)
            assert all(d['movable'] for d in state()['docks'].values())
        test('dock_native_lock_flags_menu_toggle_and_cancel',locked)
        def named():
            original=state()['docks'];assert original['propertiesDock']['visible'] and original['layersDock']['visible']
            action('保存工作区为…',workspaceName='原始',accept=True)
            assert state()['saved']==['原始']
            state(dock='propertiesDock',visible=False)
            action('保存工作区为…',workspaceName='精简',accept=True)
            action('保存工作区为…',workspaceName='取消项',accept=False);assert '取消项' not in state()['saved']
            action('切换工作区…',workspaceChoice=state()['saved'].index('原始'),accept=True)
            assert state()['docks']==original,state()
            action('切换工作区…',workspaceChoice=state()['saved'].index('精简'),accept=False)
            assert state()['docks']==original
            action('切换工作区…',workspaceChoice=state()['saved'].index('精简'),accept=True)
            assert not state()['docks']['propertiesDock']['visible']
            state(dock='propertiesDock',visible=True)
            call('window_action',text='重置当前工作区');assert not state()['docks']['propertiesDock']['visible']
            action('删除工作区…',workspaceChoice=state()['saved'].index('原始'),accept=False);assert len(state()['saved'])==2
            action('删除工作区…',workspaceChoice=state()['saved'].index('原始'),accept=True);assert state()['saved']==['精简']
            call('window_capture',path=str(OUT/'named-workspace.png'))
        test('named_save_switch_reset_delete_and_all_cancel_paths',named)
        def restart():
            pref(interfaceFontPreference=3,lockWorkspacePreference=True,movableTabsPreference=False,
                 closeTabsPreference=False,largeTabsPreference=True,restoreWorkspacePreference=True,
                 capture=str(OUT/'workspace-preferences.png'))
            assert call('window_interaction_state')['revision']==revision
            assert not state(close=True)['visible']
            child=subprocess.run([sys.executable,__file__,folder],capture_output=True,text=True,timeout=45)
            assert child.returncode==0,child.stdout+child.stderr
            s=json.loads(child.stdout.strip().splitlines()[-1])
            assert s['fontPixels']==15 and not s['movableTabs'] and not s['closeTabs'],s
            assert not s['docks']['propertiesDock']['visible'] and all(not d['movable'] for d in s['docks'].values()),s
            assert s['saved']==['精简'],s
            # Turning off restore starts with the default panels, retaining named workspaces.
            call('window');pref(restoreWorkspacePreference=False)
            child=subprocess.run([sys.executable,__file__,folder],capture_output=True,text=True,timeout=45)
            assert child.returncode==0,child.stdout+child.stderr
            s=json.loads(child.stdout.strip().splitlines()[-1]);assert s['docks']['propertiesDock']['visible'] and s['docks']['layersDock']['visible'],s
        test('close_autosave_fresh_process_actual_layout_font_flags_and_restore_off',restart)
        call('shutdown')
    report=dict(passed=sum(r['passed'] for r in records),total=len(records),tests=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
