"""Main editor crop geometry and complete Qt editing transactions."""
import ctypes,json,math,os,tempfile,traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/main-crop'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64');handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    def control(name,value):return call('window_control',name=name,value=value)
    def state(**kw):return call('window_brush',**kw)
    def drag(a,b,mods=0):call('window_gesture',x=a[0],y=a[1],endX=b[0],endY=b[1],modifiers=mods)
    def key(k):call('window_key',key=k)
    def close(a,b,tolerance=1):assert max(abs(x-y) for x,y in zip(a,b))<=tolerance,(a,b)
    results=[]
    def test(name,body):
        try:body();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(results[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-main-crop-') as folder:
        folder=Path(folder);call('init',settings=str(folder));call('window')
        yy,xx=np.mgrid[:240,:320];rgb=np.stack([xx%256,yy,(xx+yy)%256],axis=2).astype(np.uint8)
        image=folder/'grid.png';Image.fromarray(rgb).save(image)
        def scene(background=False):
            call('window_scene',width=320,height=240,layers=[dict(name='photo',path=str(image),backgroundLayer=background)])
            control('toolButton_24',True);control('cropRatio',0);control('cropReset',True)
        try:
            def handles():
                for i in range(8):
                    scene();drag((60,60),(260,180));before=state();close(before['crop'],[60,60,200,120])
                    pts=[(60,60),(160,60),(260,60),(260,120),(260,180),(160,180),(60,180),(60,120)]
                    x,y=pts[i];end=(x+(10 if x<160 else -10 if x>160 else 0),y+(10 if y<120 else -10 if y>120 else 0))
                    drag((x,y),end)
                    expected=[60+(10 if i in [0,6,7] else 0),60+(10 if i in [0,1,2] else 0),200-(10 if i in [0,2,3,4,6,7] else 0),120-(10 if i in [0,1,2,4,5,6] else 0)]
                    close(state()['crop'],expected);assert state()['history']==before['history']
                drag((160,120),(180,135));close(state()['crop'],[90,75,190,120])
                key('Shift+Right');key('Down');close(state()['crop'],[100,76,190,120])
            test('eight_handles_inside_move_keyboard_no_premature_history',handles)
            def ratios():
                scene()
                for index,ratio in [(1,4/3),(2,1),(3,4/5),(4,5/7),(5,2/3),(6,16/9)]:
                    control('cropRatio',index);r=state()['crop'];assert abs(r[2]/r[3]-ratio)<.01
                control('cropRatioWidth',7);control('cropRatioHeight',3);r=state()['crop'];assert abs(r[2]/r[3]-7/3)<.01
                control('cropSwap',True);r=state()['crop'];assert abs(r[2]/r[3]-3/7)<.01
                key('X');r=state()['crop'];assert abs(r[2]/r[3]-7/3)<.01
                x,y,w,h=r;center=(x+w/2,y+h/2);drag((x+w,y+h),(x+w+12,y+h+8),0x08000000)
                r=state()['crop'];close((r[0]+r[2]/2,r[1]+r[3]/2),center);assert abs(r[2]/r[3]-7/3)<.04
                key('Esc');assert state()['crop']==[0,0,0,0]
            test('six_ratios_custom_swap_X_alt_center_and_escape',ratios)
            def exact():
                scene();before=state();drag((40,30),(240,180));key('Return');r=state(pixels=str(OUT/'crop.png'))
                assert (r['width'],r['height'])==(200,150) and r['history']==before['history']+1
                actual=np.asarray(Image.open(OUT/'crop.png'));np.testing.assert_allclose(actual[:,:,:3],rgb[30:180,40:240],atol=1)
                key('Ctrl+Z');assert state()['hash']==before['hash'];key('Ctrl+Shift+Z');assert state()['hash']==r['hash']
                call('window_design',file=str(OUT/'crop.huelith'));call('window_open',path=str(OUT/'crop.huelith'));assert state()['hash']==r['hash']
            test('exact_crop_pixels_atomic_commit_undo_redo_project',exact)
            def rotation():
                scene();drag((60,60),(260,180));before=state()
                drag((290,120),(270,185),0x02000000);assert abs(state()['cropAngle']-30)<.01,state()
                control('cropAngle',90);call('window_capture',path=str(OUT/'crop-rotation.png'));key('Esc');assert state()['hash']==before['hash'] and state()['cropAngle']==0
                control('cropStraighten',True);drag((80,100),(240,140))
                assert abs(state()['cropAngle']+math.degrees(math.atan2(40,160)))<.5,state()
                control('cropAngle',90);key('Return');r=state(pixels=str(OUT/'rotate90.png'));assert r['history']==before['history']+1
                # The original canvas remains 320x240; the image rotates around its center.
                actual=np.asarray(Image.open(OUT/'rotate90.png'))[:,:,:3]
                expected=np.zeros_like(actual);turned=np.rot90(rgb,-1);expected[:,40:280]=turned[40:280]
                np.testing.assert_allclose(actual[4:-4,44:276],expected[4:-4,44:276],atol=1)
            test('outside_rotation_shift_angle_preview_cancel_straighten_and_pixel_geometry',rotation)
            def background():
                scene(True);before=state();drag((-20,-10),(300,220));key('Return');r=state(pixels=str(OUT/'background-expanded.png'))
                actual=np.asarray(Image.open(OUT/'background-expanded.png'));assert np.all(actual[:10,:,:3]==255) and np.all(actual[:,:20,:3]==255)
                np.testing.assert_allclose(actual[10:,20:,:3],rgb[:220,:300],atol=1)
                key('Ctrl+Z');assert state()['hash']==before['hash']
            test('background_document_crop_expansion_preserves_white_locked_base_and_undo',background)
            def retained_pixels():
                for is_background in [False,True]:
                    for delete in [False,True]:
                        scene(is_background);control('cropDeletePixels',delete)
                        drag((40,30),(240,180));key('Return')
                        saved=OUT/f'retention-{is_background}-{delete}.huelith'
                        call('window_design',file=str(saved));call('window_open',path=str(saved))
                        control('toolButton_24',True);control('cropRatio',0);control('cropReset',True)
                        control('cropDeletePixels',False)
                        drag((-40,-30),(280,210));key('Return')
                        target=OUT/f'retention-{is_background}-{delete}.png'
                        r=state(pixels=str(target));assert (r['width'],r['height'])==(320,240)
                        actual=np.asarray(Image.open(target))
                        if not delete:np.testing.assert_allclose(actual[:,:,:3],rgb,atol=1)
                        else:assert np.all(actual[:29,:,3]==0)
                        np.testing.assert_allclose(actual[31:179,41:239,:3],rgb[31:179,41:239],atol=1)
            test('delete_or_retain_pixels_background_conversion_reopen_expand',retained_pixels)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),'utf-8');raise SystemExit(not all(r['passed'] for r in results))
if __name__=='__main__':main()
