"""Production optical processing, independent spatial oracle and actual RAW controls."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import time
import traceback
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/optics'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    os.environ['QT_QPA_PLATFORM']='windows'
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command
    fn.argtypes,fn.restype=[ctypes.c_char_p],ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        result=raw(op,**kw)
        assert 'error' not in result,result
        return result
    records=[]
    def test(name,body):
        try:
            body();records.append(dict(name=name,passed=True));print('PASS',name,flush=True)
        except Exception:
            records.append(dict(name=name,passed=False,error=traceback.format_exc()));print(records[-1]['error'],flush=True)
    def wait():
        end=time.monotonic()+45
        while time.monotonic()<end:
            call('pump',waitMs=30)
            state=call('raw_window_state')
            if state['previewPresented']>0 and state['previewPresented']==state['previewRequested']:return state
        raise AssertionError(state)
    with tempfile.TemporaryDirectory(prefix='huelith-optics-') as folder:
        call('init',settings=folder)
        try:
            def identity():
                assert call('optics_probe')['identity']
                assert call('optics_probe',settings=dict(version=1,enabled=False,distortion=80,vignette=60))['identity']
                for settings in (dict(version=2),dict(version=1,distortion=101),dict(version=1,purpleHue=[40,45]),dict(version=1,profileEnabled=True),dict(version=1,removeCA='true')):
                    assert 'error' in raw('optics_probe',settings=settings)
                assert 'error' in raw('optics_probe',cancelled=True)
            test('identity_bypass_strict_recipe_validation_cancellation',identity)
            def spatial():
                y,x=np.mgrid[:48,:64]
                cx,cy=31.5,23.5
                radius=np.hypot(cx,cy)
                r2=((x-cx)**2+(y-cy)**2)/radius**2
                for distortion in (-75,0,75):
                    for vignette in (-45,45):
                        actual=np.array(call('optics_probe',alpha=True,settings=dict(version=1,distortion=distortion,vignette=vignette))['pixels']).reshape(48,64,4)
                        scale=1+distortion/250*(1-r2)
                        px=cx+(x-cx)*scale;py=cy+(y-cy)*scale
                        valid=(px>=0)&(px<=63)&(py>=0)&(py<=47)
                        edge=np.clip((np.sqrt(r2)-.25)/.75,0,1)
                        gain=2**(vignette/50*edge**2)
                        expected=np.stack([px/63*.5*gain,py/47*.5*gain,.1*gain,np.full_like(px,.5)],axis=2)
                        expected[~valid]=0
                        assert np.max(np.abs(expected-actual))<2e-5,(distortion,vignette,np.max(np.abs(expected-actual)))
            test('numpy_radial_mapping_vignette_oracle_hdr_alpha_six_cases',spatial)
            def chromatic():
                y,x=np.mgrid[:256,:256];radius=np.hypot(x-127.5,y-127.5)
                channels=[.04+.86/(1+np.exp(np.clip((radius/scale-84)*2,-80,80))) for scale in (1.006,1,.994)]
                source=np.stack([*channels,np.ones_like(radius)],axis=2)
                result=call('optics_probe',width=256,height=256,srgbLinear=True,pixels=source.reshape(-1,4).tolist(),settings=dict(version=1,removeCA=True))
                corrected=np.array(result['pixels']).reshape(256,256,4)
                ring=(radius>75)&(radius<95)
                before=np.mean((source[ring,0]-source[ring,1])**2+(source[ring,2]-source[ring,1])**2)
                after=np.mean((corrected[ring,0]-corrected[ring,1])**2+(corrected[ring,2]-corrected[ring,1])**2)
                assert after<before*.4,(before,after)
                assert np.max(np.abs(corrected[:,:,3]-1))<1e-6
                (OUT/'ca-quality.json').write_text(json.dumps(dict(synthetic_edge_error_before=before,after=after)),encoding='utf-8')
            test('image_based_chromatic_alignment_reduces_known_radial_channel_fringe',chromatic)
            def defringe():
                y,x=np.mgrid[:128,:128];radius=np.hypot(x-63.5,y-63.5)
                gray=np.where(radius<40,.9,.04)
                source=np.stack([gray,gray,gray,np.ones_like(gray)],axis=2)
                fringe=(radius>38.5)&(radius<41.5)
                source[fringe,:3]=[.7,.08,.7];source[8:24,8:24,:3]=[.7,.08,.7]
                result=call('optics_probe',width=128,height=128,srgbLinear=True,pixels=source.reshape(-1,4).tolist(),settings=dict(version=1,purple=20))
                corrected=np.array(result['pixels']).reshape(128,128,4)
                # Compare against the same fixture's sRGB -> working -> sRGB path,
                # isolating the operation from Qt's color-matrix roundtrip precision.
                baseline=np.array(call('optics_probe',width=128,height=128,srgbLinear=True,pixels=source.reshape(-1,4).tolist())['pixels']).reshape(128,128,4)
                assert np.mean(np.ptp(corrected[fringe,:3],axis=1))<.5
                assert np.array_equal(corrected[12:20,12:20],baseline[12:20,12:20])
                assert np.max(np.abs(corrected[:,:,3]-source[:,:,3]))<1e-6
            test('purple_fringe_reduction_preserves_flat_purple_surface_and_alpha',defringe)
            catalog=call('lens_profiles',directory=str(ROOT/'build/windows-release/lensfun'))['profiles']
            def database():
                assert len(catalog)>100
                usable=next(p for p in catalog if p['maker']=='Canon' and '50mm' in p['model'] and p['distortion'])
                result=call('optics_probe',settings=dict(version=1,profileEnabled=True,profileXML=usable['xml'],profileName=usable['model']))
                assert not result['identity']
                assert np.isfinite(np.array(result['pixels'])).all()
                assert result['settings']['profileXML']==usable['xml']
                assert 'error' in raw('optics_probe',settings=dict(version=1,profileEnabled=True,profileXML='<broken>'))
                (OUT/'database.json').write_text(json.dumps(dict(count=len(catalog),tested=usable['model']),indent=2),encoding='utf-8')
            test('real_lensfun_database_embedded_profile_processing_bad_profile',database)
            call('window')
            call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),denoise=False)
            wait()
            def gui():
                call('raw_window_state',click='rawSection0')
                call('raw_window_state',click='rawOpticsExpand')
                call('raw_window_state',click='rawDefringePicker');wait()
                prefix=str(OUT/'picker-')
                call('raw_window_state',previewOutput=prefix)
                hsv=np.array(Image.open(prefix+'rawPreview.png').convert('HSV'))
                candidates=np.argwhere((hsv[:,:,0]>48)&(hsv[:,:,1]>70)&(hsv[:,:,2]>40))
                assert len(candidates), 'No colored sampling patch in fixed natural fixture'
                y,x=candidates[len(candidates)//2]
                point=[(int(x)+.5)/hsv.shape[1],(int(y)+.5)/hsv.shape[0]]
                state=call('raw_window_state',stroke=[point,point])
                sampled=state['recipe']['optics']
                assert sampled['purple']==10 or sampled['green']==10,sampled
                wait()
                state=call('raw_window_state',click='rawUndo');wait()
                restored=state['recipe'].get('optics',{})
                assert restored.get('purple',0)==restored.get('green',0)==0
                call('raw_window_state',click='rawRedo');wait()
                call('raw_window_state',widget='rawOpticsPurpleRange',position=[.5,.7],mousePhase='press',modifiers=134217728)
                wait();call('raw_window_state',previewOutput=str(OUT/'aux-'))
                aux=np.array(Image.open(str(OUT/'aux-rawPreview.png')).convert('RGB'))
                assert np.array_equal(aux[:,:,0],aux[:,:,1]) and np.array_equal(aux[:,:,1],aux[:,:,2])
                call('raw_window_state',widget='rawOpticsPurpleRange',position=[.5,.7],mousePhase='release')
                wait();call('raw_window_state',previewOutput=str(OUT/'normal-'))
                normal=np.array(Image.open(str(OUT/'normal-rawPreview.png')).convert('RGB'))
                assert not np.array_equal(normal[:,:,0],normal[:,:,1])
                call('raw_window_state',click='rawOpticsReset');wait()
                call('raw_window_state',tab='rawOpticsTabs',tabIndex=1)
                state=call('raw_window_state',control='rawOpticsDistortion',value=20)
                assert state['recipe']['optics']['distortion']==20
                wait();call('pump',waitMs=300)
                state=call('raw_window_state',control='rawOpticsVignette',value=15)
                assert state['recipe']['optics']['vignette']==15
                wait()
                state=call('raw_window_state',click='rawUndo')
                assert state['recipe']['optics']['vignette']==0 and state['recipe']['optics']['distortion']==20
                call('raw_window_state',click='rawRedo');wait()
                call('raw_window_state',widget='rawOpticsGreenRange',position=[.5,.7])
                state=call('raw_window_state',widget='rawOpticsGreenRange',key=16777236)
                assert state['recipe']['optics']['greenHue']==[41,61]
                call('raw_window_state',widget='rawOpticsDistortion',capture=str(OUT/'manual.png'))
                call('raw_window_state',tab='rawOpticsTabs',tabIndex=0)
                call('raw_window_state',combo='rawOpticsMaker',text='Canon')
                usable=next(p for p in catalog if p['maker']=='Canon' and '50mm' in p['model'] and p['distortion'])
                state=call('raw_window_state',combo='rawOpticsModel',text=usable['model'])
                assert state['recipe']['optics']['profileEnabled']
                assert state['recipe']['optics']['profileXML']==usable['xml']
                assert abs(state['recipe']['optics']['cropFactor']-1.613)<.005
                wait()
                call('raw_window_state',widget='rawOpticsMaker',capture=str(OUT/'profile.png'))
                state=call('raw_window_state',click='rawOpticsEnable')
                assert not state['recipe']['optics']['enabled']
                call('raw_window_state',click='rawOpticsEnable');wait()
                state=call('raw_window_state',combo='rawOpticsMaker',text='Nikon')
                assert not state['recipe']['optics']['profileEnabled']
                assert not state['recipe']['optics']['profileXML']
                wait()
                controls=call('raw_window_state',inventory=True)['controls']
                assert next(c for c in controls if c['name']=='rawOpticsMaker')['currentText']=='Nikon'
                state=call('raw_window_state',click='rawUndo')
                assert state['recipe']['optics']['profileXML']==usable['xml']
                wait()
                state=call('raw_window_state',accept=True)
                assert state['result']==1,state
                call('raw_window_state',save=str(Path(folder)/'optics.huelith'))
            test('real_raw_tabs_manual_ranges_profile_history_bypass_save',gui)
        finally:call('shutdown')
    report=dict(passed=all(r['passed'] for r in records),checks=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False),flush=True)
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
