"""Editable design: analytic geometry, vector scaling, project roundtrip and real Qt input."""
import ctypes, json, os, tempfile, traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/design'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert 'error' not in r,r;return r
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-design-') as folder:
        call('init',settings=folder)
        try:
            def geometry():
                for kind in (0,1,2):
                    r=call('design_probe',shape=dict(version=1,kind=kind,width=80,height=60,fill='#ff0080ff',radius=10),samples=[[60,50],[0,0],[21,21]],hits=[[60,50],[0,0]])
                    assert r['pixels'][0][3]==1 and r['pixels'][1][3]==0,r
                    assert r['hits']==[True,False],r
                    assert r['pixels'][2][3]<.1,r
                for position,expected in enumerate(([0,1,1,0],[0,0,1,1],[1,1,0,0])):
                    r=call('design_probe',shape=dict(version=1,width=80,height=60,fillEnabled=False,strokeEnabled=True,strokeWidth=8,strokePosition=position),samples=[[15,40],[17,40],[22,40],[26,40]])
                    assert [p[3] for p in r['pixels']]==expected,r
            test('analytic_shape_interiors_corners_hit_and_three_stroke_alignments',geometry)
            def vector():
                s=dict(version=1,kind=1,width=60,height=40,fill='#ff20b070')
                a=call('design_probe',shape=s,width=400,height=300,scale=3,capture=str(OUT/'ellipse-full.png'))
                b=call('design_probe',shape=s,width=400,height=300,scale=3,previewEdge=200,capture=str(OUT/'ellipse-proxy.png'))
                assert np.allclose(np.array(a['bounds'])/2,b['bounds']), (a,b)
                image=np.array(Image.open(OUT/'ellipse-full.png'));alpha=image[:,:,3]/255
                expected=np.pi*90*60
                assert abs(alpha.sum()-expected)/expected<.02
                assert 0<((alpha>0)&(alpha<1)).sum()<1200
            test('vector_rerender_scaled_bounds_ellipse_analytic_area_and_proxy',vector)
            def persistence():
                cases=[dict(text=dict(version=1,html='<p><span style="font-size:22pt;color:#0099dd">中文 Huelith</span> <b>粗体</b></p>',width=220,height=120,paragraph=True)),
                       dict(shape=dict(version=1,kind=4,width=80,height=80,path=[[0,0,0],[2,80,0],[3,80,80],[3,0,80],[1,0,0]],fill='#80ff8000',strokeEnabled=True,strokeWidth=3))]
                for i,case in enumerate(cases):
                    base=call('design_probe',**case,width=400,height=300,capture=str(OUT/f'persist-{i}.png'))
                    loaded=call('design_probe',**case,width=400,height=300,file=str(Path(folder)/f'design-{i}.huelith'))
                    assert base==loaded,(base,loaded)
                    if 'text' in case:assert '中文' in loaded['text']['html']
            test('chinese_rich_text_custom_bezier_roundtrip_exact_render_and_alpha',persistence)
            def invalid():
                for shape in [dict(version=2),dict(version=1,width=0),dict(version=1,strokeWidth=-1),dict(version=1,sides=2),dict(version=1,path=[[0,0,0],[2,1,2]])]:
                    assert 'error' in raw('design_probe',shape=shape),shape
                for text in [dict(version=1,html='<img src="file:///missing">'),dict(version=1,html='x'*65537),dict(version=2,html='x')]:
                    assert 'error' in raw('design_probe',text=text),text
            test('invalid_design_data_refused',invalid)
            call('window')
            def scene():call('window_scene',width=1000,height=700,layers=[dict(name='背景',color='#ffffff',backgroundLayer=True)])
            def type_start(paragraph=False):
                call('window_key',key='T')
                call('window_gesture',x=70,y=70,endX=700 if paragraph else 70,endY=400 if paragraph else 70)
                assert call('window_design')['editing']
            def text_ui():
                scene();type_start()
                r=call('window_design',textInput='中文编辑 Huelith');assert r['live']=='中文编辑 Huelith',r
                call('window_design',key='Ctrl+A');call('window_design',control='textSize',value=18)
                call('window_design',action='text_style0')
                call('window_design',capture=str(OUT/'text-edit.png'))
                r=call('window_design',key='Ctrl+Return');assert not r['editing'] and r['history']==1,r
                assert r['layers'][-1]['kind']==7 and r['layers'][-1]['plain']=='中文编辑 Huelith',r
                assert '18pt' in r['layers'][-1]['text']['html'],r
                call('window_action',text='编辑文字')
                call('window_design',textInput='取消这些改动')
                r=call('window_design',key='Esc');assert r['history']==1 and r['layers'][-1]['plain']=='中文编辑 Huelith',r
                call('window_key',key='Ctrl+Z');assert call('window_design')['history']==0
                call('window_key',key='Ctrl+Shift+Z');assert call('window_design')['layers'][-1]['plain']=='中文编辑 Huelith'
                r=call('window_design',file=str(OUT/'editable-text.huelith'),render=str(OUT/'text-committed.png'),capture=str(OUT/'text-committed-window.png'))
                call('window_open',path=str(OUT/'editable-text.huelith'))
                call('window_action',text='编辑文字');assert call('window_design')['live']=='中文编辑 Huelith'
                call('window_design',key='Esc')
            test('canvas_chinese_input_format_commit_cancel_undo_and_reopen_editable',text_ui)
            def paragraph_ui():
                scene();type_start(True)
                r=call('window_design',textInput='段落文字可自动换行，保留中文与英文 Mixed text。\n第二段内容。')
                assert '第二段' in r['live'],r
                call('window_design',control='textAlignment',value=1)
                call('window_design',resizeText=1.15)
                r=call('window_design',key='Ctrl+Return')
                assert r['layers'][-1]['text']['paragraph'],r
                assert r['layers'][-1]['text']['width']>220,r
                assert 'align="center"' in r['layers'][-1]['text']['html'],r
                call('window_design',capture=str(OUT/'paragraph.png'),render=str(OUT/'paragraph-render.png'))
            test('drag_paragraph_box_wrap_alignment_and_multiline',paragraph_ui)
            def shape_ui():
                scene();call('window_key',key='U')
                call('window_design',control='shapeType',value=0)
                call('window_gesture',x=100,y=100,endX=350,endY=250,modifiers=0x02000000)
                r=call('window_design');s=r['layers'][-1]['shape'];assert s['width']==s['height']==250,r
                call('window_design',control='shapeRadius',value=24)
                call('window_control',name='shapeStrokeEnabled',value=True)
                call('window_design',control='shapeStrokeWidth',value=8)
                r=call('window_design',file=str(OUT/'editable-shape.huelith'),capture=str(OUT/'shape-properties.png'))
                assert r['layers'][-1]['shape']['radius']==24 and r['layers'][-1]['shape']['strokeWidth']==8,r
                call('window_key',key='Ctrl+Z');assert call('window_design')['layers'][-1]['shape']['strokeWidth']==1
                call('window_open',path=str(OUT/'editable-shape.huelith'))
                assert call('window_design')['layers'][-1]['shape']['strokeWidth']==8
            test('shape_drag_shift_square_properties_undo_and_reopen',shape_ui)
            def path_ui():
                scene();call('window_key',key='U')
                call('window_design',control='shapeType',value=0)
                call('window_gesture',x=100,y=100,endX=350,endY=300)
                call('window_key',key='A')
                call('window_gesture',x=100,y=100,endX=80,endY=80)
                r=call('window_design');shape=r['layers'][-1]['shape']
                assert shape['kind']==4 and any(p[1]<0 for p in shape['path']),r
                call('window_key',key='Ctrl+Z');assert call('window_design')['layers'][-1]['shape']['kind']==0
                call('window_key',key='P');call('window_design',control='penOperation',value=1)
                call('window_gesture',x=225,y=100,endX=225,endY=100)
                r=call('window_design');nodes=len(r['layers'][-1]['shape']['path']);assert nodes>12,r
                call('window_design',control='penOperation',value=2)
                call('window_gesture',x=225,y=100,endX=225,endY=100)
                r=call('window_design');assert len(r['layers'][-1]['shape']['path'])<nodes,r
                call('window_design',capture=str(OUT/'path-edit.png'))
            test('editable_shape_anchor_move_insert_delete_and_undo',path_ui)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
