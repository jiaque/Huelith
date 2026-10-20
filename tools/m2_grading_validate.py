"""Four-way grading: independent luminance weights and real Qt wheel interactions."""
import ctypes,json,os,tempfile,time,traceback,math,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/grading'
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
    def test(name,f):
        try:f();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-grade-') as folder:
        call('init',settings=folder)
        def weights():
            count=0
            for luminance in [.01,.05,.18,.5,1,2]:
                for blend in [0,25,50,75,100]:
                    for balance in [-60,0,60]:
                        p=max(0,min(1,luminance/(luminance+.18)+balance/200))
                        weights=[(1-p)**2,2*p*(1-p),p*p]
                        weights=[w**(2**((50-blend)/25)) for w in weights]
                        total=sum(weights);weights=[w/total for w in weights]
                        stops=sum(w*s/100 for w,s in zip(weights,[40,-20,70]))+.3
                        expected=luminance*2**stops*.5
                        result=call('tone_probe',rgba=[luminance*.5]*3+[.5],settings=dict(version=7,gradingLuminance=[40,-20,70,30],gradingBalance=balance,gradingBlending=blend))['rgba']
                        assert max(abs(v-expected) for v in result[:3])<max(2e-5,expected*2e-4),(luminance,blend,balance,result,expected)
                        assert result[3]==.5
                        count+=1
            print(dict(independent_cases=count),flush=True)
        test('ninety_independent_luminance_blending_balance_global_hdr_cases',weights)
        def persistence():
            s=dict(version=7,gradingHue=[20,50,210,130],gradingSaturation=[10,20,35,40],gradingLuminance=[-5,10,20,25],gradingBlending=73)
            call('tone_probe',rgba=[.18,.18,.18,.5],settings=s,save=str(Path(folder)/'grade.huelith'))
            for v in [1,2,3,4,5,6]:
                a=call('tone_probe',rgba=[.18,.18,.18,.5],settings=dict(version=v,gradingLuminance=[20,30,40]))
                b=call('tone_probe',rgba=[.18,.18,.18,.5],settings=dict(version=7,gradingLuminance=[20,30,40,0],legacyColorTransform=True))
                assert a['rgba']==b['rgba']
            for key,value in [('gradingBlending',-1),('gradingBlending',101),('gradingHue',[0,0,0,361]),('gradingSaturation',[0,0,0,-1])]:
                assert 'error' in raw('tone_probe',rgba=[.1,.1,.1,1],settings=dict(version=7,**{key:value}))
        test('four_ranges_project_roundtrip_legacy_three_ranges_and_invalid',persistence)
        def gui():
            call('window');path=str(ROOT/'research/m0/samples/r10-5871.CR3')
            call('raw_window',paths=[path,path],denoise=False)
            def wait():
                deadline=time.monotonic()+120
                while time.monotonic()<deadline:
                    call('pump',waitMs=30);s=call('raw_window_state')
                    if s['previewPresented'] and s['previewPresented']==s['previewRequested']:return s
                raise TimeoutError(s)
            wait();call('raw_window_state',section=5)
            for tab in range(5):
                s=call('raw_window_state',tab='toneGradingViews',tabIndex=tab,inventory=True)
                visible=[int(c['name'][-1]) for c in s['controls'] if c['name'].startswith('rawColorWheel') and c['visible']]
                assert sorted(visible)==([0,1,2] if tab==0 else [tab-1]),(tab,visible)
            call('raw_window_state',control='toneGradingHue',value=210)
            call('raw_window_state',control='toneGradingSaturation',value=35)
            call('raw_window_state',control='toneGradingLuminance',value=20)
            call('raw_window_state',control='toneGradingBlending',value=80);wait();call('pump',waitMs=450)
            s=call('raw_window_state',widget='rawColorWheel3',position=[.75,.3],modifiers=0x02000000);wait()
            assert s['recipe']['tone']['gradingHue'][3]==210,s['recipe']['tone']
            saturation=s['recipe']['tone']['gradingSaturation'][3]
            s=call('raw_window_state',widget='rawColorWheel3',position=[.25,.5],modifiers=0x04000000);wait();call('pump',waitMs=450)
            assert s['recipe']['tone']['gradingSaturation'][3]==saturation
            hue=s['recipe']['tone']['gradingHue'][3];assert abs(hue-180)<3
            call('raw_window_state',click='rawUndo');s=wait();assert s['recipe']['tone']['gradingHue'][3]==210
            call('raw_window_state',click='rawRedo');s=wait();assert s['recipe']['tone']['gradingHue'][3]==hue
            # A slow real pointer gesture must remain one undo step, even when
            # the old 250ms history debounce expires repeatedly during it.
            before=s['recipe']['tone']
            call('raw_window_state',widget='rawColorWheel3',position=[.6,.5],mousePhase='press')
            for position in [[.7,.4],[.65,.25],[.4,.2]]:
                call('raw_window_state',widget='rawColorWheel3',position=position,mousePhase='move')
                call('pump',waitMs=350)
            call('raw_window_state',widget='rawColorWheel3',position=[.4,.2],mousePhase='release')
            after=wait()['recipe']['tone'];assert after!=before
            call('raw_window_state',click='rawUndo');assert wait()['recipe']['tone']==before
            call('raw_window_state',click='rawRedo');assert wait()['recipe']['tone']==after
            # Esc restores the in-progress drag rather than closing RAW or
            # leaving a partial edit in history.
            call('raw_window_state',widget='rawColorWheel3',position=[.8,.7],mousePhase='press')
            call('pump',waitMs=350)
            call('raw_window_state',widget='rawColorWheel3',key=0x01000000)
            call('raw_window_state',widget='rawColorWheel3',position=[.8,.7],mousePhase='release')
            assert wait()['recipe']['tone']==after
            call('raw_window_state',click='rawUndo');assert wait()['recipe']['tone']==before
            call('raw_window_state',click='rawRedo');wait()
            call('raw_window_state',widget='rawColorWheel3',key=0x01000014,modifiers=0x02000000)
            keyed=wait()['recipe']['tone'];assert abs(keyed['gradingHue'][3]-(after['gradingHue'][3]+10)%360)<1e-6
            hue=keyed['gradingHue'][3]
            call('raw_window_state',click='rawSynchronize',sync=dict(groups=[5]));wait()
            s=call('raw_window_state');assert s['recipes'][1]['tone']['gradingBlending']==80
            assert s['recipes'][1]['tone']['gradingHue'][3]==hue
            call('raw_window_state',click='rawSection0');call('pump',waitMs=100)
            call('raw_window_state',widget='toneGradingViews');call('pump',waitMs=100)
            call('raw_window_state',capture=str(OUT/'global-wheel.png'))
            call('raw_window_state',tab='toneGradingViews',tabIndex=0)
            call('raw_window_state',widget='toneGradingBalance');call('pump',waitMs=100)
            call('raw_window_state',capture=str(OUT/'three-wheels.png'))
            assert call('raw_window_state',accept=True)['result']==1
            call('raw_window_state',save=str(Path(folder)/'raw-grading.huelith'))
        if '--skip-ui' not in sys.argv:
            test('five_views_actual_wheel_modifiers_history_sync_commit_save',gui)
        call('shutdown')
    report=dict(passed=sum(r['passed'] for r in records),total=len(records),tests=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
