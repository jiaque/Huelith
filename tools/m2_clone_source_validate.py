"""Clone-source transform oracle and real preview/edit/history/persistence."""
import base64, ctypes, json, os, tempfile
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/clone-source'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    enc=lambda a:base64.b64encode(a.tobytes()).decode()
    results=[]
    with tempfile.TemporaryDirectory(prefix='huelith-clone-') as folder:
        call('init',settings=folder)
        try:
            y,x=np.mgrid[:96,:128];a=np.ones((96,128,4),dtype='<f4');a[:,:,0]=x/128;a[:,:,1]=y/96;a[:,:,2]=.4
            mask=np.zeros((96,128),np.uint8);mask[40:70,55:80]=255
            for scale_x,scale_y,angle,flip_x,flip_y in [(1,1,0,False,False),(2,.5,0,False,False),(1,1,30,False,False),(1.5,2,-25,True,True)]:
                r=call('brush_probe',retouch=True,operation=0,strength=1,width=128,height=96,data=enc(a),mask=enc(mask),
                       anchor=[60,50],offset=[-25,-15],scaleX=scale_x,scaleY=scale_y,rotation=angle,flipX=flip_x,flipY=flip_y)
                actual=np.frombuffer(base64.b64decode(r['data']),dtype='<f4').reshape(a.shape)
                radians=np.deg2rad(angle);dx=x-60;dy=y-50
                sx=np.floor(35+(dx*np.cos(radians)+dy*np.sin(radians))/scale_x*(-1 if flip_x else 1)).astype(int)
                sy=np.floor(35+(-dx*np.sin(radians)+dy*np.cos(radians))/scale_y*(-1 if flip_y else 1)).astype(int)
                valid=(mask>0)&(sx>=0)&(sx<128)&(sy>=0)&(sy<96)
                expected=a.copy();expected[valid]=a[sy[valid],sx[valid]]
                assert np.max(np.abs(actual-expected))<2e-6,(scale_x,angle,float(np.max(np.abs(actual-expected))))
            results.append(dict(name='four_transform_numpy_reference_cases',passed=True))
            call('window');path=Path(folder)/'grid.png';Image.fromarray((a[:,:,:3]*255).astype(np.uint8)).resize((384,288)).save(path)
            call('window_scene',width=384,height=288,layers=[dict(name='photo',path=str(path))])
            for name,value in [('toolButton_16',True),('cloneSourceOpen',True),('cloneScaleX',150),('cloneScaleY',75),('cloneRotation',25),('cloneFlipX',True)]:
                call('window_control',name=name,value=value)
            before=call('window_brush')
            for phase in ['press','release']:call('window_mouse',phase=phase,x=80,y=90,modifiers=134217728)
            call('window_mouse',phase='move',x=220,y=180)
            call('window_capture',path=str(OUT/'overlay.png'))
            assert call('window_brush')['hash']==before['hash']
            call('window_gesture',x=220,y=180,endX=245,endY=185)
            after=call('window_brush');assert after['hash']!=before['hash'] and after['history']==before['history']+1
            call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==before['hash']
            call('window_key',key='Ctrl+Shift+Z');assert call('window_brush')['hash']==after['hash']
            saved=str(OUT/'clone.huelith');call('window_design',file=saved);call('window_open',path=saved)
            assert call('window_brush')['hash']==after['hash']
            results.append(dict(name='real_source_overlay_transform_stroke_undo_reopen',passed=True))
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results,indent=2),'utf-8')
    print(results,flush=True)

if __name__=='__main__':main()
