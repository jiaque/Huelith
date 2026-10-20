"""Layer style coverage/placement and production dialog transactions."""
import copy,ctypes,json,os,tempfile,traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/layer-styles'
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
    with tempfile.TemporaryDirectory(prefix='huelith-styles-') as folder:
        call('init',settings=folder)
        shape=dict(version=1,width=40,height=30,fill='#ff70a0d0')
        base=call('design_probe',shape=shape);defaults=base['styles']
        def style(index,**kw):
            value=copy.deepcopy(defaults);value['entries'][index].update(enabled=True,**kw);return value
        try:
            def shadows():
                for angle,inside,outside in [(180,[65,35],[15,35]),(90,[35,55],[35,15]),(0,[15,35],[65,35])]:
                    s=style(1,size=0,distance=10,opacity=1,globalLight=False,angle=angle)
                    r=call('design_probe',shape=shape,styles=s,samples=[inside,outside,[35,35]])
                    assert np.allclose(np.array(r['pixels'])[:,3],[1,0,1]),r
                s=style(1,size=0,distance=10,opacity=.4,globalLight=True,angle=0);s['globalAngle']=180
                r=call('design_probe',shape=shape,styles=s,samples=[[65,35]])
                assert abs(r['pixels'][0][3]-.4)<1e-6,r
            test('shadow_angle_distance_opacity_and_global_light',shadows)
            def stroke():
                for position,points,expected in [(0,[[17,35],[13,35]],[1,0]),(1,[[17,35],[21,35]],[0,1]),(2,[[19,35],[16,35]],[1,0])]:
                    s=style(0,size=4,color='#ffff0000',opacity=1,position=position)
                    r=call('design_probe',shape=shape,styles=s,samples=points)
                    assert np.allclose(np.array(r['pixels'])[:,3],expected),r
            test('euclidean_stroke_outer_inner_center',stroke)
            def canvas_edge():
                s=style(0,size=4,color='#ffff0000',opacity=1,position=1)
                r=call('design_probe',shape=shape,styles=s,x=0,y=0,capture=str(OUT/'stroke-canvas-edge.png'))
                image=np.array(Image.open(OUT/'stroke-canvas-edge.png'))
                assert np.allclose(image[15,1,:3],[255,0,0],atol=1)
                s=style(4,size=12,color='#ffff0000',opacity=1,blend=0)
                call('design_probe',shape=shape,styles=s,x=0,y=0,capture=str(OUT/'glow-canvas-edge.png'))
                image=np.array(Image.open(OUT/'glow-canvas-edge.png'))
                assert image[15,0,0]>image[15,15,0],image[15,:20,0]
            test('interior_stroke_and_glow_at_canvas_boundary',canvas_edge)
            def interiors():
                for index in range(6):
                    s=style(index,color='#ffff0000',opacity=1,size=7,distance=8)
                    path=OUT/f'style-{index}.png'
                    r=call('design_probe',shape=shape,styles=s,capture=str(path))
                    assert r['hash']!=base['hash'],index
                    loaded=call('design_probe',shape=shape,styles=s,file=str(Path(folder)/f'{index}.huelith'))
                    assert loaded['hash']==r['hash'] and loaded['styles']==s,(index,loaded)
                    if index in (2,4,5):
                        image=np.array(Image.open(path)); assert image[:,:,3].sum()==40*30*255,index
                    s['enabled']=False;assert call('design_probe',shape=shape,styles=s)['hash']==base['hash']
                image=np.array(Image.open(OUT/'style-5.png'));assert np.allclose(image[30,30,:3],[255,0,0],atol=1)
            test('six_styles_real_pixels_interior_alpha_roundtrip_and_bypass_identity',interiors)
            def modes():
                for blend in range(24):
                    r=call('design_probe',shape=shape,styles=style(5,blend=blend,color='#ffdb3066',opacity=.6),samples=[[35,35]])
                    assert np.isfinite(r['pixels']).all() and r['pixels'][0][3]==1,(blend,r)
                for field,value in [('size',-1),('distance',1001),('spread',2),('blend',24)]:
                    assert 'error' in raw('design_probe',shape=shape,styles=style(1,**{field:value}))
            test('all_24_style_blends_finite_alpha_and_invalid_parameters',modes)
            call('window');call('window_scene',width=800,height=600,layers=[])
            call('window_key',key='U');call('window_gesture',x=200,y=180,endX=500,endY=400)
            def ui():
                before=call('window_design')['history']
                r=call('window_style',index=1,styleDistance=25,styleSize=12,accept=False,capture=str(OUT/'dialog-cancel.png'))
                assert r['ready'] and r['history']==before and not r['styles']['entries'][1]['enabled'],r
                r=call('window_style',index=1,styleDistance=25,styleSize=12,accept=True,capture=str(OUT/'dialog-accept.png'))
                assert r['ready'] and r['history']==before+1,r
                assert r['styles']['entries'][1]['distance']==25,r
                call('window_design',file=str(OUT/'styled-shape.huelith'),capture=str(OUT/'styled-shape-window.png'))
                call('window_key',key='Ctrl+Z');assert not call('window_design')['layers'][-1]['styles']['entries'][1]['enabled']
                call('window_open',path=str(OUT/'styled-shape.huelith'))
                assert call('window_design')['layers'][-1]['styles']['entries'][1]['distance']==25
            test('actual_style_dialog_background_preview_cancel_commit_undo_reopen',ui)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
