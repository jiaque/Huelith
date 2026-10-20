"""Curves: independent SciPy PCHIP, analytical parametric bounds and actual RAW controls."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
from scipy.interpolate import PchipInterpolator
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/curves'
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert 'error' not in r,r;return r
    def curve(points,x,version=8,**kw):return np.array(call('curve_probe',settings=dict(version=version,curve=points),values=list(x),**kw)['values'])
    def state(**kw):return call('raw_window_state',**kw)
    def wait():
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            call('pump',waitMs=30);r=state()
            if r['idle'] and r['previewPresented']>0 and r['previewRequested']==r['previewPresented']:return r
        raise TimeoutError(r)
    def tone():return state()['recipe'].get('tone',{})
    results=[]
    def test(name,fn):
        try:fn();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(results[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-curves-') as folder:
        call('init',settings=folder);call('window')
        try:
            def spline():
                rng=np.random.default_rng(9281);x=np.linspace(0,1,301);maximum=0
                for n in [3,5,9,20]:
                    for case in range(10):
                        xp=np.linspace(0,1,n);yp=rng.random(n);points=np.stack([xp,yp],axis=1).tolist();ref=PchipInterpolator(xp,yp)
                        out=curve(points,x);maximum=max(maximum,float(np.max(abs(out-ref(x)))))
                        assert np.max(abs(out-ref(x)))<2e-12
                        ext=curve(points,[-.3,1.4]);assert np.allclose(ext,[yp[0]-.3*ref.derivative()(0),yp[-1]+.4*ref.derivative()(1)],atol=1e-12)
                print(dict(scipy_cases=40,max_error=maximum),flush=True)
            test('forty_independent_pchip_curves_and_linear_hdr_extrapolation',spline)
            def legacy():
                points=[[0,.1],[.3,.7],[.6,.4],[1,.9]];x=np.linspace(0,1,101)
                out=curve(points,x,version=7);assert np.max(abs(out-np.interp(x,*np.array(points).T)))<1e-12
                settings=call('curve_probe',settings=dict(version=7,curve=points),values=[.25])['settings'];assert settings['version']==8 and settings['legacyCurveInterpolation']
                again=call('curve_probe',settings=settings,values=x.tolist());assert np.array_equal(out,again['values'])
            test('legacy_piecewise_linear_recipe_upgrade_pixel_contract',legacy)
            def parameters():
                x=np.linspace(0,1,201)
                for strength in [-100,-50,0,50,100]:
                    r=call('curve_probe',settings=dict(version=8,parametricCurve=[strength]*4),parametric=True,values=x.tolist())
                    expected=x+x*(1-x)*strength/100;assert np.max(abs(np.array(r['values'])-expected))<1e-12
                for splits in [[.2,.4,.8],[.25,.5,.75]]:
                    out=call('curve_probe',settings=dict(version=8,parametricCurve=[-40,20,60,-10],curveSplits=splits),parametric=True,values=[-.5,0,1,2])['values'];assert out==[-.5,0,1,2]
                for bad in [[.4,.3,.7],[0,.5,.9],[.2,.3,1]]:assert 'error' in raw('curve_probe',settings=dict(version=8,curveSplits=bad),values=[.5])
                assert 'error' in raw('curve_probe',settings=dict(version=8,parametricCurve=[0,101,0,0]),values=[.5])
            test('four_regions_analytical_extremes_hdr_and_invalid_boundaries',parameters)
            def pixels():
                settings=dict(version=8,parametricCurve=[-30,0,40,10],curve=[[0,0],[.4,.5],[1,1]],channelCurves=[[[0,0],[.6,.65],[1,1]],[[0,0],[1,1]],[[0,0],[1,1]]])
                r=call('tone_probe',settings=settings,rgba=[.1,.2,.3,.5],save=str(OUT/'curve.huelith'));assert r['rgba'][3]==.5 and np.isfinite(r['rgba']).all()
                bypass=call('tone_probe',settings=dict(settings,sections=[True,True,True,False,True,True,True,True,True]),rgba=[.1,.2,.3,.5]);assert np.allclose(bypass['rgba'],[.1,.2,.3,.5],atol=1e-7)
                zero=call('tone_probe',settings=settings,rgba=[0,0,0,0]);assert zero['rgba']==[0,0,0,0]
            test('actual_fp32_pipeline_alpha_bypass_and_project_roundtrip',pixels)
            path=str(ROOT/'research/m0/samples/r10-5871.CR3');call('raw_window',paths=[path,path],denoise=False);wait();state(section=3)
            def param_ui():
                state(tab='toneCurveTabs',tabIndex=0);state(control='toneParametric3',value=40);wait();assert tone()['parametricCurve']==[0,0,0,40]
                state(curveGesture=dict(split=True,**{'from':[.25,0],'to':[.32,0]}));wait();assert abs(tone()['curveSplits'][0]-.32)<.003
                state(click='rawUndo');wait();assert tone()['curveSplits']==[.25,.5,.75]
                state(click='rawRedo');wait();state(capture=str(OUT/'parametric.png'))
            test('actual_parametric_tab_sliders_region_handles_undo_redo',param_ui)
            def points_ui():
                state(tab='toneCurveTabs',tabIndex=1)
                before=tone()['curve'];state(curveGesture=dict(**{'from':[.45,.5],'to':[.48,.65]},holdMs=550));wait();assert len(tone()['curve'])==3
                state(click='rawUndo');wait();assert tone()['curve']==before
                state(click='rawRedo');wait()
                state(control='toneCurveOutput',value=180);wait();assert abs(tone()['curve'][1][1]-180/255)<1e-8
                state(curveKey=16777235,modifiers=33554432);wait();assert abs(tone()['curve'][1][1]-190/255)<1e-8
                before=tone()['curve'];state(curveGesture=dict(**{'from':[before[1][0],before[1][1]],'to':[.6,.4]},escape=True));wait();assert tone()['curve']==before
                state(curveKey=16777223);wait();assert len(tone()['curve'])==2
                state(tab='toneCurveTabs',tabIndex=2);state(curveGesture=dict(**{'from':[.5,.6],'to':[.5,.7]}));wait();assert len(tone()['channelCurves'][0])==3
                state(capture=str(OUT/'point-red.png'))
            test('actual_point_channels_numeric_keyboard_escape_delete_single_drag_undo',points_ui)
            def sync():
                expected=tone();state(click='rawSynchronize',sync=dict(groups=[3]));state(index=1);r=wait()
                other=r['recipes'][1]['tone']
                for key in ['curve','channelCurves','parametricCurve','curveSplits','legacyCurveInterpolation']:assert other[key]==expected[key],key
                state(accept=True);state(save=str(OUT/'raw-curves.huelith'))
            test('new_curve_fields_multifile_sync_full_commit_save',sync)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');raise SystemExit(not all(r['passed'] for r in results))
if __name__=='__main__':main()
