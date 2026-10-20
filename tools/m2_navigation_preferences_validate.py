"""Real canvas gestures for scrubby/animated zoom, inertial pan and synchronized documents."""
import ctypes,json,os,tempfile,time,traceback,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/navigation-preferences'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    def state():return call('window_interaction_state')
    def mouse(phase,x,y=250):return call('window_mouse',phase=phase,space='widget',x=x,y=y)
    def pref(accept=True,**kw):return call('window_action',text='工具与画面操控…',dialog=dict(accept=accept,**kw))
    def key(value,**kw):return call('window_key',key=value,**kw)
    def control(name,value):return call('window_control',name=name,value=value)
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-navprefs-') as folder:
        call('init',settings=folder);call('window');call('window_scene',width=1600,height=1200,layers=[])
        revision=state()['revision'];call('window_chrome',click=11)
        def scrub():
            pref(scrubbyZoomPreference=True,overscrollPreference=True)
            call('window_view',zoom=1);mouse('press',400);mouse('move',560);mouse('release',560)
            assert abs(state()['zoom']-math.e)<1e-9,state()
            mouse('press',560);mouse('move',400);mouse('release',400)
            assert abs(state()['zoom']-1)<1e-9,state()
            mouse('press',400);mouse('release',400);assert state()['zoom']==2
            pref(accept=False,scrubbyZoomPreference=False)
            call('window_view',zoom=1);mouse('press',400);mouse('move',560);mouse('release',560)
            assert abs(state()['zoom']-math.e)<1e-9
        test('scrubby_horizontal_drag_both_directions_click_and_preference_cancel',scrub)
        def animated():
            pref(scrubbyZoomPreference=False,animatedZoomPreference=True)
            call('window_view',zoom=1);mouse('press',400);mouse('release',400)
            values=[state()['zoom']]
            for _ in range(12):call('pump',waitMs=20);values.append(state()['zoom'])
            assert values[-1]==2 and any(1<v<2 for v in values),values
            assert all(a<=b for a,b in zip(values,values[1:])),values
            (OUT/'animation.json').write_text(json.dumps(values),encoding='utf-8')
            pref(animatedZoomPreference=False)
            mouse('press',400);mouse('release',400);assert state()['zoom']==4
        test('animated_zoom_monotonic_intermediate_frames_and_exact_endpoint',animated)
        def bounds():
            pref(overscrollPreference=False,flickPanningPreference=False)
            key('Ctrl+0');before=state();key('Space');mouse('press',400);mouse('move',900);mouse('release',900);key('Space',release=True)
            after=state();assert before['viewX']==after['viewX'] and before['viewY']==after['viewY'],(before,after)
            pref(overscrollPreference=True)
            key('Space');mouse('press',400);mouse('move',600);mouse('release',600);key('Space',release=True)
            assert abs(state()['viewX']-after['viewX']-200)<1e-7,state()
        test('overscroll_disabled_fit_center_and_enabled_real_pan',bounds)
        def inertia():
            pref(flickPanningPreference=True,overscrollPreference=True);key('Ctrl+0')
            key('Space');mouse('press',400);call('pump',waitMs=20);mouse('move',450);mouse('release',450);key('Space',release=True)
            x=state()['viewX'];call('pump',waitMs=120);assert state()['viewX']>x+5,(x,state())
            call('pump',waitMs=1100);x=state()['viewX'];call('pump',waitMs=100);assert abs(state()['viewX']-x)<1
            pref(flickPanningPreference=False);key('Space');mouse('press',400);mouse('move',450);mouse('release',450);key('Space',release=True)
            x=state()['viewX'];call('pump',waitMs=120);assert state()['viewX']==x
        test('inertial_pan_continues_then_decays_and_disabled_stops_at_release',inertia)
        def sync():
            call('window_new',width=800,height=600)
            pref(zoomAllPreference=True,panAllPreference=True,flickPanningPreference=False)
            call('window_view',zoom=2);key('Space');mouse('press',400);mouse('move',440);mouse('release',440);key('Space',release=True)
            a=state();call('window_tab',index=0);b=state()
            assert b['zoom']==a['zoom']==2,(a,b)
            def center(s,w,h):return ((s['viewportWidth']/2-s['viewX'])/s['zoom']/w,(s['viewportHeight']/2-s['viewY'])/s['zoom']/h)
            assert np_close(center(a,800,600),center(b,1600,1200)),(a,b)
            assert b['revision']==revision
            pref(zoomAllPreference=False,panAllPreference=False)
            call('window_view',zoom=.5);call('window_tab',index=1);assert state()['zoom']==2
        test('multi_document_zoom_and_relative_pan_sync_without_image_history_changes',sync)
        call('shutdown')
    report=dict(passed=sum(r['passed'] for r in records),total=len(records),tests=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(report['passed']!=report['total'])
def np_close(a,b):return max(abs(x-y) for x,y in zip(a,b))<1e-7
if __name__=='__main__':raise SystemExit(main())
