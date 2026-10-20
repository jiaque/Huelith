"""Post-crop film effects: independent geometry, grain statistics, and real Qt controls."""
import ctypes,json,os,tempfile,time,traceback,sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/film-effects'

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
    def probe(settings=None,**kw):
        working=kw.pop('working',False)
        r=call('film_probe',settings=dict(version=6,**(settings or {})),rgba=kw.pop('rgba',[.2,.2,.2,.5]),**kw)
        if working:
            return np.array(r['workingPixels']).reshape(r['height'],r['width'],4), np.array(r['inputWorking'])
        return np.array(r['pixels']).reshape(r['height'],r['width'],4)
    records=[]
    def test(name,fn):
        try:fn();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-film-') as folder:
        call('init',settings=folder)
        def geometry():
            w,h=128,96;y,x=np.mgrid[:h,:w]
            for midpoint in [0,50,100]:
                for feather in [0,50,100]:
                    radius=np.sqrt(((x+.5)*2/w-1)**2+((y+.5)*2/h-1)**2)
                    boundary=.15+midpoint/100*.85;f=feather/100*.8
                    if f:
                        coverage=np.clip((radius-boundary+f/2)/f,0,1);coverage=coverage**2*(3-2*coverage)
                    else:coverage=(radius>=boundary).astype(float)
                    a,source=probe(dict(vignette=-60,vignetteStyle=2,vignetteMidpoint=midpoint,vignetteFeather=feather),working=True)
                    expected=source[0]*(1-.6*coverage)
                    assert np.max(np.abs(a[:,:,0]-expected))<2e-6,(midpoint,feather,float(np.max(np.abs(a[:,:,0]-expected))),a[0,0].tolist(),float(expected[0,0]),source.tolist())
                    assert np.all(a[:,:,3]==.5)
            a=probe(dict(vignette=-60,vignetteStyle=2,vignetteRoundness=-100))
            b=probe(dict(vignette=-60,vignetteStyle=2,vignetteRoundness=100))
            assert np.max(np.abs(a-b))>.03
        test('nine_midpoint_feather_numpy_geometries_roundness_and_alpha',geometry)
        def cropped():
            settings=dict(vignette=-65,vignetteStyle=1,vignetteRoundness=40)
            a=probe(settings,crop=dict(x=.25,y=.25,endX=.75,endY=.75))
            b=probe(settings,width=64,height=48)
            assert a.shape==b.shape and np.max(np.abs(a-b))<2e-5
            for style in [0,1]:
                a=probe(dict(vignette=-80,vignetteStyle=style,vignetteHighlights=0),rgba=[.9,.9,.9,1])
                b=probe(dict(vignette=-80,vignetteStyle=style,vignetteHighlights=100),rgba=[.9,.9,.9,1])
                assert b[0,0,0]>a[0,0,0]+.2
            a=probe(dict(vignette=60,vignetteStyle=2,vignetteHighlights=100))
            b=probe(dict(vignette=60,vignetteStyle=2,vignetteHighlights=0))
            assert np.array_equal(a,b)
        test('post_crop_coordinate_reference_and_highlight_protection_dependencies',cropped)
        def grain():
            small=probe(dict(grain=70,grainSize=5,grainRoughness=20))
            large=probe(dict(grain=70,grainSize=90,grainRoughness=20))
            rough=probe(dict(grain=70,grainSize=90,grainRoughness=100))
            assert np.array_equal(small,probe(dict(grain=70,grainSize=5,grainRoughness=20)))
            def roughness(a):return np.mean(np.diff(a[:,:,0],axis=1)**2)
            assert roughness(small)>roughness(large)*2
            assert roughness(rough)>roughness(large)
            assert np.std(large[:,:,0])>.002 and abs(large[:,:,0].mean()-.2)<.008
            dark=probe(dict(grain=30,grainSize=70,grainRoughness=65),rgba=[.002,.002,.002,.5])
            assert np.mean(dark[:,:,0]<=0)<.01
            assert np.std(dark[:,:,0])<.001, float(np.std(dark[:,:,0]))
            a=probe(dict(grain=0,grainSize=100,grainRoughness=100),rgba=[-.1,2,4,.5])
            assert np.max(np.abs(a-[-.1,2,4,.5]))<2e-5
        test('deterministic_grain_size_autocorrelation_roughness_zero_identity_hdr',grain)
        def persistence():
            settings=dict(vignette=-44,vignetteStyle=1,vignetteMidpoint=22,vignetteRoundness=35,vignetteFeather=77,vignetteHighlights=64,grain=40,grainSize=60,grainRoughness=70)
            probe(settings,save=str(Path(folder)/'film.huelith'))
            for field,value in [('vignetteStyle',3),('vignetteStyle',.5),('grainSize',-1),('vignetteFeather',101)]:
                assert 'error' in raw('film_probe',rgba=[.2,.2,.2,.5],settings=dict(version=6,**{field:value}))
            assert 'error' in raw('film_probe',rgba=[.2,.2,.2,.5],settings=dict(version=6,grain=40),cancelled=True)
            for version in [1,2,3,4,5]:
                r=call('film_probe',rgba=[.2,.2,.2,.5],settings=dict(version=version,vignette=-40,grain=50))
                assert r['settings']['legacyFilmEffects']
        test('project_pixels_persistence_old_recipe_version_and_invalid_cancel',persistence)
        def gui():
            call('window');path=str(ROOT/'research/m0/samples/r10-5871.CR3')
            call('raw_window',paths=[path,path],denoise=False)
            def wait():
                deadline=time.monotonic()+120
                while time.monotonic()<deadline:
                    call('pump',waitMs=30);s=call('raw_window_state')
                    if s['previewPresented'] and s['previewPresented']==s['previewRequested']:return s
                raise TimeoutError(s)
            wait();s=call('raw_window_state',section=6,inventory=True)
            c={i['name']:i for i in s['controls']};assert not c['toneGrainSize']['enabled'] and not c['toneVignetteMidpoint']['enabled']
            for name,value in [('toneVignette',-50),('toneVignetteMidpoint',35),('toneVignetteRoundness',25),('toneVignetteFeather',70),('toneVignetteHighlights',60),('toneGrain',30),('toneGrainSize',40),('toneGrainRoughness',65)]:
                call('raw_window_state',control=name,value=value)
            wait();call('pump',waitMs=400)
            s=call('raw_window_state',combo='toneVignetteStyle',comboIndex=2,inventory=True)
            c={i['name']:i for i in s['controls']};assert not c['toneVignetteHighlights']['enabled'] and c['toneGrainSize']['enabled']
            call('raw_window_state',combo='toneVignetteStyle',comboIndex=1);wait();call('pump',waitMs=400)
            call('raw_window_state',control='toneGrainSize',value=70);wait();call('pump',waitMs=400)
            call('raw_window_state',click='rawUndo');assert wait()['recipe']['tone']['grainSize']==40
            call('raw_window_state',click='rawRedo');assert wait()['recipe']['tone']['grainSize']==70
            call('raw_window_state',click='rawSynchronize',sync=dict(groups=[2]));wait()
            s=call('raw_window_state');assert s['recipes'][1]['tone']['grainSize']==70 and s['recipes'][1]['tone']['vignetteHighlights']==60
            call('raw_window_state',widget='toneGrainRoughness',capture=str(OUT/'effects-panel.png'))
            call('raw_window_state',index=1);wait()
            assert call('raw_window_state',accept=True)['result']==1
            call('raw_window_state',save=str(Path(folder)/'raw-film.huelith'))
        if '--skip-ui' not in sys.argv:
            test('actual_raw_controls_dependencies_undo_redo_sync_full_resolution_save',gui)
        call('shutdown')
    report=dict(passed=sum(r['passed'] for r in records),total=len(records),tests=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
