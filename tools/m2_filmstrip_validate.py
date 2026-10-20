"""RAW actual filmstrip selection, rating/filter, multiedit isolation, undo and output."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/filmstrip'
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))];os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    def state(**kw):return call('raw_window_state',**kw)
    def wait():
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            call('pump',waitMs=30);r=state()
            if r['idle'] and r['previewPresented']>0 and r['previewPresented']==r['previewRequested']:return r
        raise TimeoutError(r)
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-filmstrip-') as folder:
        call('init',settings=folder);call('window');path=str(ROOT/'research/m0/samples/r10-5871.CR3')
        call('raw_window',paths=[path,path,path],denoise=False);wait()
        try:
            def ratings():
                state(film=dict(row=0,star=4));assert state()['film']['ratings']==[4,0,0]
                state(film=dict(row=2,modifiers=33554432));wait();assert state()['film']['selected']==[0,1,2],state()['film']
                state(click='rawRating3');assert state()['film']['ratings']==[3,3,3]
                state(click='rawUndo');wait();assert state()['film']['ratings']==[4,0,0]
                state(click='rawRedo');wait();assert state()['film']['ratings']==[3,3,3]
                state(film=dict(row=1));wait();state(film=dict(key='5'));assert state()['film']['ratings']==[3,5,3]
                state(combo='rawFilmFilter',comboIndex=5);assert state()['film']['hidden']==[0,2]
                state(combo='rawFilmFilter',comboIndex=1);assert state()['film']['hidden']==[0,1,2]
                state(click='rawRating0');assert state()['film']['ratings']==[3,5,3],'hidden current photo changed'
                state(combo='rawFilmFilter',comboIndex=0)
            test('thumbnail_stars_shift_selection_keyboard_filter_undo_hidden_protection',ratings)
            def multi():
                state(film=dict(row=0));wait();state(control='toneSaturation',value=10);state(exposure=1);wait();call('pump',waitMs=400)
                state(film=dict(row=1));wait();state(control='toneSaturation',value=30);state(exposure=-1);wait();call('pump',waitMs=400)
                state(film=dict(row=0,modifiers=67108864));wait();assert state()['film']['selected']==[0,1],state()['film']
                before=state()['recipes'];state(exposure=.7);wait();call('pump',waitMs=400);after=state()['recipes']
                assert after[0]['tone']['exposure']==.7 and after[1]['tone']['exposure']==.7
                assert after[0]['tone']['saturation']==10 and after[1]['tone']['saturation']==30
                assert after[2]==before[2]
                state(click='rawUndo');wait();r=state()['recipes'];assert r[0]['tone']==before[0]['tone'] and r[1]['tone']==before[1]['tone']
                state(click='rawRedo');wait();r=state()['recipes'];assert r[0]['tone']==after[0]['tone'] and r[1]['tone']==after[1]['tone']
                state(capture=str(OUT/'multi-edit.png'))
            test('multiple_selected_exposure_preserves_other_per_photo_fields_atomic_undo',multi)
            def reject_output():
                state(film=dict(row=2));wait();state(click='rawRejectPhoto');assert state()['recipes'][2]['rejected']
                state(combo='rawFilmFilter',comboIndex=7);assert state()['film']['hidden']==[0,1]
                state(click='rawUndo');wait();assert not state()['recipes'][2].get('rejected',False)
                state(click='rawRedo');wait();assert state()['recipes'][2]['rejected']
                state(combo='rawFilmFilter',comboIndex=0);state(film=dict(row=1));wait();state(click='rawRejectPhoto')
                state(accept=True);r=state();assert r['result']==1 and r['count']==1,r['status']
                state(save=str(Path(folder)/'rated.huelith'))
                assert r['recipes'][0]['rating']==3
            test('rejection_filter_undo_excluded_output_raw_rating_project_persistence',reject_output)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8');raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
