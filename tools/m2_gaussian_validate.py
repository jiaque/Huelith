"""Independent SciPy Gaussian reference and production non-destructive filter workflows."""
import base64, ctypes, json, os, tempfile, traceback
from pathlib import Path
import numpy as np
from scipy.ndimage import gaussian_filter
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/gaussian'

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
    def probe(a,radius=5,mask=None,**kw):
        if mask is None:mask=np.full(a.shape[:2],255,np.uint8)
        r=call('brush_probe',gaussian=True,width=a.shape[1],height=a.shape[0],data=encode(a.astype('<f4')),mask=encode(mask),radius=radius,**kw)
        return np.frombuffer(base64.b64decode(r['data']),dtype='<f4').reshape(a.shape),r
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-gaussian-') as tmp:
        call('init',settings=tmp)
        try:
            rng=np.random.default_rng(79)
            a=rng.uniform(-.1,3,(48,64,4)).astype(np.float32);a[:,:,3]=rng.uniform(0,1,(48,64));a[:,:,:3]*=a[:,:,3,None]
            def reference():
                for radius in [.1,.5,1,2.3,4]:
                    actual,_=probe(a,radius)
                    expected=gaussian_filter(a.astype(float),(radius,radius,0),radius=(int(np.ceil(3.5*radius)),int(np.ceil(3.5*radius)),0),mode='nearest')
                    assert np.max(abs(actual-expected))<3e-7,np.max(abs(actual-expected))
                impulse=np.zeros((512,512,4),np.float32);impulse[256,256]=[2,1,.5,1]
                for radius in [5,12,50]:
                    actual,_=probe(impulse,radius)
                    expected=gaussian_filter(impulse.astype(float),(radius,radius,0),mode='nearest',truncate=4)
                    relative=np.max(abs(actual-expected))/np.max(expected)
                    assert relative<.08,(radius,relative)
                    assert abs(actual[:,:,3].sum()-1)<.001
                print('large-radius approximation peak-relative bound <8%',flush=True)
            test('small_radius_exact_SciPy_large_radius_impulse_energy_HDR_premult_alpha',reference)
            def contracts():
                mask=np.zeros((48,64),np.uint8);mask[:,20:40]=128
                blurred,_=probe(a,2)
                actual,r=probe(a,2,mask=mask,masked=True,amount=.6,file=str(OUT/'gaussian.huelith'))
                expected=a+(blurred-a)*(mask/255.*.6)[:,:,None]
                assert np.max(abs(actual-expected))<3e-7 and r['roundtrip']
                assert r['proxyRadius']==1
                for kwargs in [dict(radius=0),dict(enabled=False),dict(amount=0)]:
                    result,_=probe(a,**kwargs);assert np.array_equal(result,a)
                for kwargs in [dict(radius=-1),dict(radius=1001),dict(cancelled=True)]:
                    assert 'error' in raw('brush_probe',gaussian=True,width=64,height=48,data=encode(a),mask=encode(mask),**kwargs)
                flat=np.ones((32,48,4),np.float32);flat[:,:,:3]=[3,-.1,1]
                result,_=probe(flat,1000);assert np.max(abs(result-flat))<1e-6
            test('selection_strength_disable_cancel_bounds_constant_edges_schema13_source_preserved',contracts)
            call('window')
            pattern=np.zeros((180,240,3),np.uint8);pattern[:,120:]=255
            path=Path(tmp)/'edge.png';Image.fromarray(pattern).save(path)
            def scene():call('window_scene',width=240,height=180,layers=[dict(name='edge',path=str(path))])
            def dialog(steps,accept=True,text='高斯模糊…'):
                return call('window_action',text=text,dialog=dict(steps=steps,accept=accept))
            def ui():
                scene();before=call('window_brush')
                dialog([dict(control='effectBlurRadius',value=8)],False)
                assert call('window_brush')['hash']==before['hash']
                preset=Path(tmp)/'blur.hfx'
                dialog([dict(control='effectBlurRadius',value=8)])
                after=call('window_brush',pixels=str(OUT/'blur8.png'))
                assert after['hash']!=before['hash'] and after['history']==before['history']+1
                pixel=np.asarray(Image.open(OUT/'blur8.png'))[90,115,0];assert 10<pixel<230,pixel
                call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==before['hash']
                call('window_key',key='Ctrl+Shift+Z');assert call('window_brush')['hash']==after['hash']
                dialog([dict(control='effectBlurRadius',value=15),dict(click='effectSavePreset',file=str(preset))],text='智能效果…')
                updated=call('window_brush');assert updated['hash']!=after['hash']
                assert json.loads(preset.read_text('utf-8'))['blurRadius']==15
                call('window_design',file=str(OUT/'ui-blur.huelith'));call('window_open',path=str(OUT/'ui-blur.huelith'))
                assert call('window_brush')['hash']==updated['hash']
                scene();dialog([dict(click='effectLoadPreset',file=str(preset))],text='智能效果…')
                assert call('window_brush')['hash']==updated['hash']
            test('actual_filter_menu_live_radius_cancel_single_undo_reedit_preset_project',ui)
            def performance():
                large=np.ones((1200,1800,4),np.float32);large[:,:900,:3]=0
                _,r=probe(large,80)
                print('2.16MP radius80 ms',r['elapsedMs'],flush=True)
                assert r['elapsedMs']<10000,r
            test('2MP_large_radius_linear_cost',performance)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
