"""Production FP32 retouch reference checks and actual Qt brush transactions."""
import base64, ctypes, json, os, tempfile, traceback
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/retouch-tone'

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
    def encode(a):return base64.b64encode(a.tobytes()).decode()
    def probe(a,m,**kw):
        r=call('brush_probe',retouch=True,width=a.shape[1],height=a.shape[0],data=encode(a.astype('<f4')),mask=encode(m),**kw)
        return np.frombuffer(base64.b64decode(r['data']),dtype='<f4').reshape(a.shape)
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-retouch-') as tmp:
        call('init',settings=tmp)
        try:
            rng=np.random.default_rng(329)
            a=rng.uniform(0,2,(16,64,4)).astype(np.float32);a[:,:,3]=rng.uniform(.01,1,(16,64));a[:,:,:3]*=a[:,:,3,None]
            a[0]=0
            mask=rng.integers(0,256,(16,64),dtype=np.uint8)
            selection=rng.integers(0,256,(16,64),dtype=np.uint8)
            def ranges():
                alpha=a[:,:,3].astype(float)
                lum=np.divide(a[:,:,:3]@np.array([.2627,.678,.0593]),alpha,out=np.zeros_like(alpha),where=alpha>0)
                encoded=np.clip(lum,0,1)**(1/2.2)
                def smooth(lo,hi):
                    t=np.clip((encoded-lo)/(hi-lo),0,1);return t*t*(3-2*t)
                weights=[1-smooth(.15,.65),4*smooth(.1,.9)*(1-smooth(.1,.9)),smooth(.35,.85)]
                maximum=0
                for op in [5,6]:
                    for index,weight in enumerate(weights):
                        for protect in [False,True]:
                            gain=2**((1 if op==5 else -1)*weight)
                            target=np.where(lum>1,1+(lum-1)*gain,lum*gain/(1+lum*(gain-1))) if protect else lum*gain
                            ratio=np.divide(target,lum,out=gain.copy(),where=lum>1e-12)
                            expected=a.astype(float).copy();k=mask/255.*selection/255.*.73
                            expected[:,:,:3]*=(1-k+k*ratio)[:,:,None]
                            actual=probe(a,mask,operation=op,range=index,protect=protect,strength=.73,selection=encode(selection))
                            error=float(np.max(abs(actual-expected)));maximum=max(maximum,error)
                            assert error<1e-6,error
                            assert np.max(abs(actual[:,:,3]-a[:,:,3]))<1e-7
                            assert np.isfinite(actual).all()
                print('reference max error',maximum,flush=True)
            test('12_dodge_burn_range_protection_HDR_alpha_soft_selection_reference',ranges)
            def sponge():
                luma=(a[:,:,:3]@np.array([.2627,.678,.0593]))[:,:,None]
                mx=a[:,:,:3].max(2);mn=a[:,:,:3].min(2)
                sat=np.divide(mx-mn,mx,out=np.zeros_like(mx),where=mx>1e-8)
                for increase in [False,True]:
                    for vibrance in [False,True]:
                        amount=(1 if increase else -1)*(1-.75*sat if vibrance else np.ones_like(sat))
                        expected=a.astype(float).copy()
                        expected[:,:,:3]+=(a[:,:,:3]-luma)*(mask/255.*.6*amount)[:,:,None]
                        actual=probe(a,mask,operation=7,increase=increase,vibrance=vibrance,strength=.6)
                        assert np.max(abs(actual-expected))<1e-6
                gray=np.ones((8,8,4),np.float32);gray[:,:,:3]=.23
                assert np.max(abs(probe(gray,np.full((8,8),255,np.uint8),operation=7,increase=True)-gray))<1e-7
                assert np.array_equal(probe(a,mask,strength=0),a)
                assert 'error' in raw('brush_probe',retouch=True,width=64,height=16,data=encode(a),mask=encode(mask),range=3)
            test('sponge_both_modes_vibrance_luminance_neutral_identity_validation',sponge)
            call('window')
            rgb=np.full((240,320,3),128,np.uint8);path=Path(tmp)/'gray.png';Image.fromarray(rgb).save(path)
            def scene(tool):
                call('window_scene',width=320,height=240,layers=[dict(name='pixels',path=str(path))])
                call('window_control',name=f'toolButton_{tool}',value=True)
                call('window_brush',settings=dict(version=1,diameter=60,hardness=1,pressureSize=False))
                call('window_control',name='photoStrength',value=80)
            def ui():
                scene(21);call('window_control',name='photoToneRange',value=1)
                call('window_control',name='photoProtectTones',value=True)
                initial=call('window_brush')
                call('window_gesture',x=100,y=100,endX=100,endY=100)
                done=call('window_brush',pixels=str(OUT/'dodge.png'))
                assert done['history']==initial['history']+1 and done['hash']!=initial['hash']
                pic=np.asarray(Image.open(OUT/'dodge.png'));assert pic[100,100,0]>128,pic[100,100]
                call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==initial['hash']
                call('window_gesture',x=100,y=100,endX=100,endY=100,modifiers=134217728)
                call('window_brush',pixels=str(OUT/'alt-burn.png'))
                pic=np.asarray(Image.open(OUT/'alt-burn.png'));assert pic[100,100,0]<128,pic[100,100]
                prior=call('window_brush')
                call('window_mouse',phase='press',x=200,y=100);call('window_mouse',phase='move',x=230,y=100)
                call('window_key',key='Esc');assert call('window_brush')['hash']==prior['hash']
                call('window_design',file=str(OUT/'retouch.huelith'));call('window_open',path=str(OUT/'retouch.huelith'))
                assert call('window_brush')['hash']==prior['hash']
                call('window_control',name='toolButton_21',value=True)
                call('window_capture',path=str(OUT/'tone-options.png'))
            test('actual_range_exposure_protection_Alt_reverse_single_undo_escape_reopen',ui)
            def sponge_ui():
                colored=rgb.copy();colored[:]=[160,110,70];Image.fromarray(colored).save(path)
                scene(23);call('window_control',name='photoSpongeMode',value=1)
                call('window_control',name='photoSpongeVibrance',value=False)
                before=call('window_brush');call('window_gesture',x=100,y=100,endX=100,endY=100)
                call('window_brush',pixels=str(OUT/'saturate.png'))
                pixel=np.asarray(Image.open(OUT/'saturate.png'))[100,100,:3].astype(float)
                assert pixel.max()-pixel.min()>90,pixel
                call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==before['hash']
                call('window_control',name='photoSpongeMode',value=0)
                call('window_gesture',x=100,y=100,endX=100,endY=100)
                call('window_brush',pixels=str(OUT/'desaturate.png'))
                pixel=np.asarray(Image.open(OUT/'desaturate.png'))[100,100,:3].astype(float)
                assert pixel.max()-pixel.min()<90,pixel
                call('window_capture',path=str(OUT/'sponge-options.png'))
            test('actual_sponge_mode_flow_vibrance_controls_and_undo',sponge_ui)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
