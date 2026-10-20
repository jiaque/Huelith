"""Targeted adjustment: independent angular weights and actual Qt RAW gestures."""
import ctypes,json,os,tempfile,time,traceback,colorsys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/target-adjustment'
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
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
    def tone():return call('curve_probe',settings=state()['recipe'].get('tone',{}),values=[])['settings']
    results=[]
    def test(name,fn):
        try:fn();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(results[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-target-') as folder:
        call('init',settings=folder);call('window')
        try:
            def bands():
                centers=np.array([0,30,60,120,180,240,270,300])
                for mode,key in enumerate(['hue','saturationBands','luminanceBands']):
                    for hue in [0,15,30,60,90,120,179,210,240,270,300,330,359]:
                        rgb=colorsys.hsv_to_rgb(hue/360,.8,.8)
                        r=call('target_probe',mode=mode,rgb=rgb,amount=40);assert r['started']
                        d=np.abs(centers-hue);weights=np.maximum(0,1-np.minimum(d,360-d)/60);weights/=max(weights)
                        assert np.max(abs(np.array(r['settings'][key])-40*weights))<.03,(hue,key,r)
                assert not call('target_probe',mode=0,rgb=[.4]*3,amount=60)['started']
                r=call('target_probe',mode=1,rgb=[1,0,0],amount=500);assert max(r['settings']['saturationBands'])==100
                r=call('target_probe',mode=0,rgb=[1,0,0],amount=50,cancel=True);assert r['settings']['hue']==[0]*8
            test('39_independent_hue_band_weights_gray_ignore_clamp_cancel',bands)
            def curve():
                for value,expected in [(0,[40,0,0,0]),(.5,[0,20,20,0]),(1,[0,0,0,40])]:
                    r=call('target_probe',mode=3,rgb=[value]*3,amount=40)
                    assert np.allclose(r['settings']['parametricCurve'],expected,atol=.01),r
            test('independent_parametric_region_weights',curve)
            call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),denoise=False);wait();state(section=4)
            def actual_hsl():
                state(tab='rawHslTabs',tabIndex=1);state(click='rawHslTarget');wait();before=tone()['saturationBands']
                state(stroke=[[.5,.5],[.5,.35],[.5,.3]],strokeHoldMs=550);wait();after=tone()['saturationBands']
                assert after!=before,(before,after)
                assert all(a>=b for a,b in zip(after,before))
                state(click='rawUndo');wait();assert tone()['saturationBands']==before
                state(click='rawRedo');wait();assert tone()['saturationBands']==after
                state(stroke=[[.5,.5],[.5,.2],[.5,.1]],strokeEscape=True);wait();assert tone()['saturationBands']==after
                state(tab='rawHslTabs',tabIndex=0);wait();before=tone()['hue'];state(stroke=[[.5,.5],[.5,.3],[.5,.25]]);wait();assert tone()['hue']!=before
                state(capture=str(OUT/'hsl-target.png'))
            test('real_raw_adjacent_bands_long_drag_one_undo_redo_escape_tab_switch',actual_hsl)
            def actual_curve():
                state(section=3);state(click='toneCurveTarget');wait();before=tone()['parametricCurve']
                state(stroke=[[.5,.5],[.5,.3],[.5,.2]]);wait();after=tone()['parametricCurve'];assert after!=before
                state(click='rawUndo');wait();assert tone()['parametricCurve']==before
                state(click='rawRedo');wait();assert tone()['parametricCurve']==after
                state(escape=True);wait();state(capture=str(OUT/'curve-target.png'))
                state(tab='toneCurveTabs',tabIndex=1);state(widget='toneCurvePreset',key=16777237);wait();assert len(tone()['curve'])==5
            test('real_raw_curve_target_switch_undo_redo_exit',actual_curve)
            state(reject=True)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');raise SystemExit(not all(r['passed'] for r in results))
if __name__=='__main__':main()
