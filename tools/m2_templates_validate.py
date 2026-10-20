"""Editable frame layout geometry, embedded source persistence and actual template menus."""
import ctypes,json,os,tempfile,traceback
from pathlib import Path
from PIL import Image
import numpy as np
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/templates'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command
    fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert 'error' not in r,r;return r
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-layout-') as folder:
        call('init',settings=folder)
        source=Path(folder)/'photo.png';Image.new('RGB',(320,180),(220,80,20)).save(source)
        try:
            def layout():
                r=call('layout_probe',rows=2,columns=2,image=str(source),file=str(OUT/'layout.huelith'),capture=str(OUT/'layout.png'))
                assert r['frames']==[[30,30,360,220],[410,30,360,220],[30,270,360,220],[410,270,360,220]],r
                assert r['same'] and r['layers']==6,r
                image=np.array(Image.open(OUT/'layout.png'))
                assert np.allclose(image[100,100,:3],[220,80,20],atol=1)
                assert np.all(image[100,400,:3]==255) and np.all(image[260,100,:3]==255)
            test('four_frames_analytic_spacing_cover_fit_clipping_and_persistence',layout)
            def bad():
                for args in [dict(rows=0),dict(columns=11),dict(margin=1000),dict(gap=-1),dict(caption=600),dict(width=10)]:
                    assert 'error' in raw('layout_probe',**args),args
                r=call('layout_probe',missingFont='Huelith_Test_Missing_Font_4765',file=str(OUT/'missing-font.huelith'))
                assert r['fonts']==['Huelith_Test_Missing_Font_4765'] and r['same'],r
            test('invalid_layout_rejected_and_missing_font_retained_reported',bad)
            call('window')
            def ui():
                before=call('window_state')['count']
                call('window_action',text='新建布局模板…',dialog=dict(accept=False))
                assert call('window_state')['count']==before
                call('window_action',text='新建布局模板…',dialog=dict(accept=True,layoutWidth=800,layoutHeight=600,layoutTitle='可编辑拼图'))
                r=call('window_design');assert len([l for l in r['layers'] if l['frame']])==4,r
                assert r['layers'][-1]['plain']=='可编辑拼图',r
                # Tree rows are topmost first: title, fourth frame, third frame...
                call('window_select',index=1)
                call('window_action',text='置入当前相框…',dialog=dict(accept=True,path=str(source)))
                r=call('window_design',file=str(OUT/'ui-layout.huelith'),capture=str(OUT/'ui-layout.png'))
                assert r['layers'][-2]['children']==2,r
                call('window_key',key='Ctrl+Z');assert call('window_design')['layers'][-2]['children']==1
                call('window_open',path=str(OUT/'ui-layout.huelith'))
                assert call('window_design')['layers'][-2]['children']==2
                source.unlink()
                call('window_open',path=str(OUT/'ui-layout.huelith'))
                assert call('window_design')['layers'][-2]['children']==2
                call('window_design',render=str(OUT/'embedded-source.png'))
            test('template_menu_cancel_create_place_undo_reopen_missing_external_source',ui)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
