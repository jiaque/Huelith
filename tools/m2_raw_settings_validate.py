"""RAW settings subsets, persistent presets, clipboard and per-photo snapshots via production Qt."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/raw-settings'/os.environ.get('QT_QPA_PLATFORM','windows')
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))];os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert 'error' not in r,r;return r
    def state(**kw):return call('raw_window_state',**kw)
    def wait():
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            call('pump',waitMs=30);r=state()
            if r['idle'] and r['previewPresented']>0 and r['previewPresented']==r['previewRequested']:return r
        raise TimeoutError(r)
    def manage(action,steps):return state(settingsAction=action,settingsSteps=steps)['settingsProbe']
    results=[]
    def test(name,fn):
        try:fn();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(results[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-raw-settings-') as folder:
        call('init',settings=folder);call('window')
        try:
            def transfer():
                source=dict(tone=dict(version=8,exposure=2,saturation=30,parametricCurve=[10,20,30,40],hue=[20]*8),crop=[.1,.1,.8,.8],metadata=dict(camera='SOURCE'),snapshots=[dict(name='do not copy')],denoise=False)
                target=dict(tone=dict(version=8,exposure=-1,saturation=-40),metadata=dict(camera='TARGET'),snapshots=[dict(name='keep')],crop=[0,0,1,1])
                for group in range(14):
                    r=call('raw_recipe_probe',source=source,target=target,groups=[group]);out=r['target'];assert out['metadata']==target['metadata'] and out['snapshots']==target['snapshots']
                    assert out['tone']['exposure']==(2 if group==0 else -1)
                    assert out['tone']['saturation']==(30 if group==1 else -40)
                    assert out['crop']==(source['crop'] if group==9 else target['crop'])
                    assert 'metadata' not in r['preset']['recipe'] and 'snapshots' not in r['preset']['recipe']
                r=call('raw_recipe_probe',source=source,target=target,groups=list(range(14)));assert r['target']['tone']['parametricCurve']==[10,20,30,40]
                (OUT/'roundtrip.hrawpreset').write_text(json.dumps(r['preset']),encoding='utf-8');assert call('raw_recipe_probe',data=(OUT/'roundtrip.hrawpreset').read_text(encoding='utf-8'))['preset']==r['preset']
                for bad in ['{}','[]','bad',json.dumps(dict(r['preset'],version=99)),json.dumps(dict(r['preset'],groups=[1]*14))]:assert 'error' in raw('raw_recipe_probe',data=bad)
                assert 'error' in raw('raw_recipe_probe',source=dict(tone=dict(version=8,parametricCurve=[0,101,0,0])),groups=[3])
            test('fourteen_group_isolation_metadata_snapshot_protection_export_validation',transfer)
            path=str(ROOT/'research/m0/samples/r10-5871.CR3');call('raw_window',paths=[path,path],denoise=False);wait()
            def preset():
                state(exposure=1.5);wait()
                r=manage('rawPresets',[dict(name='Exposure only',groups=[0]),dict(click='rawPresetNew'),dict(capture=str(OUT/'presets.png'))]);assert r['names']==['Exposure only'] and not r['status'],r
                r=manage('rawPresets',[dict(name='Exposure only'),dict(click='rawPresetNew')]);assert r['status'] and len(r['names'])==1
                state(exposure=-.5);wait();r=manage('rawPresets',[dict(index=0),dict(click='rawPresetApply')]);wait();assert state()['recipe']['tone']['exposure']==1.5
                state(click='rawUndo');wait();assert state()['recipe']['tone']['exposure']==-.5
                state(click='rawRedo');wait();assert state()['recipe']['tone']['exposure']==1.5
                r=manage('rawPresets',[dict(index=0,name='Light saved'),dict(click='rawPresetRename')]);assert r['names']==['Light saved'],r
            test('actual_preset_create_duplicate_apply_undo_redo_rename',preset)
            def snapshot():
                state(exposure=.7);wait();r=manage('rawSnapshots',[dict(name='Before color'),dict(click='rawPresetNew')]);assert r['names']==['Before color'],r
                state(exposure=2);wait();r=manage('rawSnapshots',[dict(index=0),dict(click='rawPresetApply')]);wait();assert state()['recipe']['tone']['exposure']==.7
                assert len(state()['recipe']['snapshots'])==1
                state(click='rawUndo');wait();assert state()['recipe']['tone']['exposure']==2
                state(click='rawRedo');wait();assert state()['recipe']['tone']['exposure']==.7
                state(index=1);wait();r=manage('rawSnapshots',[dict(capture=str(OUT/'empty-other-snapshots.png'))]);assert r['names']==[],r
                state(index=0);wait()
            test('actual_per_photo_snapshot_restore_preserve_collection_undo_isolation',snapshot)
            def clipboard():
                state(settingsAction='rawCopySettings');state(index=1);wait();state(exposure=-2);wait()
                r=manage('rawPasteSettings',[dict(groups=[0]),dict(click='rawPresetApply')]);wait();assert state()['recipes'][1]['tone']['exposure']==.7,r
                assert not state()['recipes'][1].get('snapshots')
                state(click='rawUndo');wait();assert state()['recipes'][1]['tone']['exposure']==-2
                state(click='rawRedo');wait();assert state()['recipes'][1]['tone']['exposure']==.7
            test('actual_clipboard_subset_paste_new_photo_single_undo',clipboard)
            def persistent():
                state(accept=True);state(save=str(OUT/'snapshots.huelith'))
                call('raw_window',path=path,denoise=False);wait();r=manage('rawPresets',[dict(index=0),dict(click='rawPresetApply')]);wait();assert state()['recipe']['tone']['exposure']==1.5,r
                r=manage('rawPresets',[dict(index=0),dict(click='rawPresetDelete')]);assert r['names']==[],r
                state(reject=True)
            test('project_save_and_preset_survives_new_raw_session_delete',persistent)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');raise SystemExit(not all(r['passed'] for r in results))
if __name__=='__main__':main()
