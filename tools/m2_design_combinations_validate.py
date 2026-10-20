"""Compound paths, editable warped text, guarded rasterization and persistent pixels."""
import ctypes,json,os,tempfile
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/design-combinations'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64');handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    def scene():call('window_scene',width=600,height=450,layers=[dict(name='Background',color='#ffffff',backgroundLayer=True)])
    def control(name,value):call('window_control',name=name,value=value)
    def pixels(name):
        path=OUT/(name+'.png');call('window_design',render=str(path));return np.asarray(Image.open(path))
    records=[]
    with tempfile.TemporaryDirectory(prefix='huelith-design-combo-') as folder:
        call('init',settings=folder);call('window')
        try:
            y,x=np.mgrid[:450,:600]
            a=(x>=80)&(x<260)&(y>=80)&(y<240)
            b=(x>=180)&(x<360)&(y>=150)&(y<310)
            for name,expected in [('联合形状',a|b),('减去顶层形状',a&~b),('与形状区域相交',a&b),('排除重叠形状',a^b)]:
                scene();call('window_key',key='U');call('window_design',control='shapeType',value=0)
                call('window_gesture',x=80,y=80,endX=260,endY=240)
                call('window_gesture',x=180,y=150,endX=360,endY=310)
                call('window_select',index=0);call('window_select',index=1,control=True)
                before=call('window_design')['history'];call('window_action',text=name)
                state=call('window_design');assert len(state['layers'])==2 and state['layers'][-1]['shape']['kind']==4,state
                actual=pixels(name);selected=actual[:,:,:3].mean(axis=2)<128
                # Avoid the one-pixel antialias boundary; interiors must match analytic sets.
                stable=(x%10==5)&(y%10==5);assert np.array_equal(selected[stable],expected[stable]),name
                assert state['history']==before+1
                call('window_key',key='Ctrl+Z');assert len(call('window_design')['layers'])==3
                call('window_key',key='Ctrl+Shift+Z')
                path=str(OUT/(name+'.huelith'));call('window_design',file=path);call('window_open',path=path)
                assert np.array_equal(pixels(name+'-reopened'),actual)
            records.append(dict(case='four compound Boolean paths pixels undo reopen',passed=True))
            # Rasterization is explicit; a brush cannot silently destroy editable content.
            scene();call('window_key',key='T');call('window_gesture',x=70,y=70,endX=70,endY=70)
            call('window_design',textInput='中文 Warp 2026');call('window_design',control='textSize',value=18)
            call('window_design',key='Ctrl+Return')
            before=call('window_design');call('window_key',key='B');call('window_gesture',x=90,y=90,endX=120,endY=95)
            assert call('window_design')['history']==before['history'] and call('window_design')['layers'][-1]['kind']==7
            call('window_key',key='V');control('warpMode',True)
            call('window_mouse',phase='press',warp=4);call('window_mouse',phase='move',warp=4,dy=25);call('window_mouse',phase='release',warp=4)
            call('window_key',key='Return')
            warped=call('window_probe')['layers'][-1];assert warped['warped'],warped
            call('window_action',text='编辑文字');call('window_design',textInput='中文 Edited 2026',capture=str(OUT/'warped-text-edit.png'))
            call('window_design',key='Ctrl+Return')
            after=call('window_probe')['layers'][-1]
            assert after['warped'] and after['warpCenterY']==warped['warpCenterY']
            rendered=pixels('warped-text');call('window_action',text='栅格化文字 / 形状')
            assert call('window_design')['layers'][-1]['kind']==0
            np.testing.assert_allclose(pixels('rasterized'),rendered,atol=1)
            call('window_key',key='Ctrl+Z');assert call('window_design')['layers'][-1]['kind']==7
            assert call('window_probe')['layers'][-1]['warped']
            saved=str(OUT/'warped-editable.huelith');call('window_design',file=saved);call('window_open',path=saved)
            assert np.array_equal(pixels('warped-reopened'),rendered)
            records.append(dict(case='text paint guard warp edit raster appearance undo persistence',passed=True))
            scene();call('window_key',key='U');call('window_design',control='shapeType',value=0)
            call('window_gesture',x=80,y=80,endX=260,endY=240)
            call('window_key',key='M');call('window_gesture',x=80,y=80,endX=180,endY=240)
            call('window_action',text='蒙版');call('window_key',key='Ctrl+D')
            call('window_select',index=0,column=0)
            call('window_style',index=1,styleDistance=12,styleSize=6,accept=True)
            call('window_key',key='V');control('warpMode',False)
            call('window_action',text='水平居中画布')
            state=call('window_probe')['layers'][-1]
            assert state['masked'] and state['maskTx']!=0,state
            before=pixels('masked-styled-aligned-shape')
            call('window_action',text='栅格化文字 / 形状')
            np.testing.assert_allclose(pixels('masked-styled-raster'),before,atol=1)
            call('window_key',key='Ctrl+Z')
            assert call('window_design')['layers'][-1]['kind']==8
            path=str(OUT/'masked-styled-shape.huelith');call('window_design',file=path);call('window_open',path=path)
            assert np.array_equal(pixels('masked-styled-reopened'),before)
            records.append(dict(case='shape mask shadow alignment rasterization undo output persistence',passed=True))
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,indent=2),'utf-8');print(records,flush=True)

if __name__=='__main__':main()
