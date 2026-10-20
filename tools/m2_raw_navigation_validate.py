"""Real RAW viewport navigation and pixel-aligned before/after comparison."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/raw-navigation'
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64');handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))];os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
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
    def nav(**kw):return state(nav=kw)['navigation']
    def prefs(**kw):state(preferences=dict(accept=True,**kw));wait()
    def capture(name):
        state(previewOutput=str(OUT/(name+'-')))
        return np.array(Image.open(OUT/(name+'-rawPreview.png')))
    results=[]
    def test(name,fn):
        try:fn();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(results[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-raw-nav-') as folder:
        call('init',settings=folder);call('window')
        try:
            path=str(ROOT/'research/m0/samples/r10-5871.CR3')
            call('raw_window',paths=[path,path],denoise=False);wait();initial=state()['recipe'];baseline=capture('baseline')
            def clicks():
                state(click='rawZoom100');wait();before=state(navPoint=[.4,.4])['navigation'][0];state(click='rawZoomTool');nav(drag=[[.4,.4],[.4,.4]])
                after=state(navPoint=[.4,.4])['navigation'][0];assert abs(after['scale']/before['scale']-2)<1e-6
                assert np.linalg.norm(np.array(after['point'])-before['point'])<3/after['scale']
                nav(drag=[[.4,.4],[.4,.4]],modifiers=134217728);assert abs(state()['scale']-before['scale'])<1e-9
                state(click='rawZoomOutTool');nav(drag=[[.5,.5],[.5,.5]]);assert state()['scale']<before['scale'];state(click='rawZoomFit');wait()
            test('actual_zoom_in_out_alt_reverse_and_pointer_anchor',clicks)
            def wheel():
                prefs(wheelZoomPreference=False);before=state()['scale'];nav(wheel=120);assert state()['scale']==before
                nav(wheel=120,modifiers=134217728);assert abs(state()['scale']/before-1.2)<1e-9
                prefs(wheelZoomPreference=True);before=state()['scale'];nav(wheel=-120);assert abs(state()['scale']/before-1/1.2)<1e-9
                for _ in range(70):nav(wheel=120)
                assert state()['scale']==32;state(click='rawZoomFit');wait();assert state()['recipe']==initial
            test('raw_wheel_preference_alt_override_repeated_zoom_bound_no_recipe_edit',wheel)
            def gestures():
                prefs(scrubbyZoomPreference=True,animatedZoomPreference=False);state(click='rawZoomTool');before=state()['scale'];nav(drag=[[.3,.4],[.45,.4],[.45,.4]]);assert state()['scale']>before
                state(click='rawZoomFit');wait();prefs(scrubbyZoomPreference=False);before=state()['scale'];nav(drag=[[.25,.25],[.6,.65],[.6,.65]]);assert state()['scale']>before*1.5
                state(click='rawZoom100');wait();state(click='rawHandTool');before=state()['navigation'][0]['center'];nav(drag=[[.4,.4],[.55,.5],[.55,.5]]);after=state()['navigation'][0]['center'];assert np.linalg.norm(np.array(after)-before)>20
                state(click='rawZoomFit');wait();prefs(animatedZoomPreference=True);state(click='rawZoom100');wait();before=state()['scale'];state(click='rawZoomTool');nav(drag=[[.5,.5],[.5,.5]]);call('pump',waitMs=180);assert abs(state()['scale']/before-2)<.001
                prefs(animatedZoomPreference=False);state(click='rawZoomFit');wait()
            test('scrubby_rectangle_hand_and_animated_zoom_real_mouse',gestures)
            def pan_preferences():
                prefs(overscrollPreference=False,flickPanningPreference=False);state(click='rawZoomFit');wait();before=state()['navigation'][0]['center']
                nav(key=32);nav(drag=[[.3,.4],[.6,.5],[.6,.5]]);nav(key=32,release=True);after=state()['navigation'][0]['center'];assert np.linalg.norm(np.array(after)-before)<4/state()['scale'],(before,after,state()['scale'])
                prefs(overscrollPreference=True);nav(key=32);nav(drag=[[.3,.4],[.5,.5],[.5,.5]]);nav(key=32,release=True);assert np.linalg.norm(np.array(state()['navigation'][0]['center'])-after)>30
                prefs(flickPanningPreference=True);state(click='rawHandTool');nav(drag=[[.3,.4],[.5,.5],[.5,.5]]);before=state()['navigation'][0]['center'];call('pump',waitMs=160);after=state()['navigation'][0]['center'];assert np.linalg.norm(np.array(after)-before)>10
                state(click='rawZoomTool');prefs(flickPanningPreference=False);state(click='rawZoomFit');wait()
                nav(key=49,modifiers=67108864);wait();assert state()['scale']==1
                nav(key=43,modifiers=67108864);assert state()['scale']==2
                nav(key=45,modifiers=67108864);assert state()['scale']==1
                nav(key=48,modifiers=67108864);wait();assert state()['scale']<1
            test('space_pan_overscroll_flick_and_standard_keyboard_zoom_steps',pan_preferences)
            def split():
                state(exposure=1);wait();after=capture('after');assert np.mean(abs(after.astype(float)-baseline))>5
                for mode in [1,2]:
                    state(combo='rawCompareLayout',comboIndex=mode);wait();pixels=capture('split'+str(mode));h,w=after.shape[:2]
                    if mode==1:
                        assert np.array_equal(pixels[:,:w//2],baseline[:,:w//2]);assert np.array_equal(pixels[:,w//2:],after[:,w//2:])
                    else:
                        assert np.array_equal(pixels[:h//2],baseline[:h//2]);assert np.array_equal(pixels[h//2:],after[h//2:])
                state(capture=str(OUT/'split-window.png'))
            test('split_views_exact_before_after_pixel_halves',split)
            def linked():
                for mode in [3,4]:
                    state(combo='rawCompareLayout',comboIndex=mode);wait();r=state()['navigation'];assert r[1]['visible'] and abs(r[0]['scale']-r[1]['scale'])<1e-10
                    prefs(wheelZoomPreference=True);nav(wheel=120,view='rawBeforePreview',position=[.4,.3]);r=state()['navigation'];assert abs(r[0]['scale']-r[1]['scale'])<1e-10
                    assert np.linalg.norm(np.array(r[0]['center'])-r[1]['center'])<4/r[0]['scale'],r
                    state(click='rawZoom100');wait();nav(view='rawBeforePreview',drag=[[.3,.4],[.45,.5],[.45,.5]]);r=state()['navigation'];assert np.linalg.norm(np.array(r[0]['center'])-r[1]['center'])<4,r
                    state(click='rawZoomFit');wait()
                state(click='rawSetBefore');wait();after=capture('new-before');before=np.array(Image.open(OUT/'new-before-rawBeforePreview.png'));assert np.array_equal(before,after)
                state(capture=str(OUT/'linked-window.png'));state(combo='rawCompareLayout',comboIndex=0);wait();assert not state()['navigation'][1]['visible']
                assert state()['recipe']['tone']['exposure']==1
            test('two_view_bidirectional_zoom_pan_alignment_100percent_set_before',linked)
            def photo_memory():
                state(click='rawZoom100');wait();state(click='rawHandTool')
                nav(drag=[[.3,.4],[.5,.5],[.5,.5]])
                before=state()['navigation'][0]
                state(index=1);wait();state(click='rawZoomFit');wait()
                other=state()['navigation'][0]
                state(index=0);wait();after=state()['navigation'][0]
                assert abs(after['scale']-before['scale'])<1e-8
                assert np.linalg.norm(np.array(after['center'])-before['center'])<3,(before,after)
                state(index=1);wait();assert abs(state()['scale']-other['scale'])<1e-8
            test('filmstrip_preserves_each_photo_zoom_pan_and_detail_mode',photo_memory)
            state(reject=True)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');raise SystemExit(not all(r['passed'] for r in results))
if __name__=='__main__':main()
