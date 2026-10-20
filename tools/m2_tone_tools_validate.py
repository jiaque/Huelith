"""Real main-window point picker, range preview and Alt detail preview transactions."""
import ctypes,json,os,tempfile,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/main-tone-tools'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-main-tone-') as folder:
        call('init',settings=folder);call('window')
        try:
            call('window_open',path=str(ROOT/'research/m2/semantic/samples/ADE_val_00000001.jpg'))
            def transaction(detail,accept):
                before=call('window_probe')['history']
                r=call('window_tone_tools',detail=detail,accept=accept,capture=str(OUT/f'{"detail" if detail else "point"}-{accept}.png'))
                assert not r['timedOut'] and r['stage']==4,r
                if detail:
                    assert r['auxiliary']==1,r
                    if accept:assert r['tone']['details']['masking']>0
                else:
                    assert r['samples']==1 and r['escaped'],r
                    if accept:assert len(r['tone']['pointColors'])==1
                assert call('window_probe')['history']==before+int(accept)
                if accept:
                    call('window_key',key='Ctrl+Z');assert call('window_probe')['history']==before
                (OUT/f'{"detail" if detail else "point"}-{accept}.json').write_text(json.dumps(r),encoding='utf-8')
            test('main_point_sampling_visualize_escape_cancel_no_document_change',lambda:transaction(False,False))
            test('main_point_sampling_commit_one_history_entry_and_undo',lambda:transaction(False,True))
            test('main_detail_alt_preview_release_cancel_no_document_change',lambda:transaction(True,False))
            test('main_detail_alt_preview_not_saved_commit_parameter_and_undo',lambda:transaction(True,True))
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
