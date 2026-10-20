"""RAW preferences exercise real Qt transactions, cache budgets and source-preserving defaults."""
import ctypes,json,os,tempfile,time,traceback,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/raw-preferences'
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    def prefs(**kw):return call('window_action',text='RAW与性能…',dialog=kw)
    def performance():return call('canvas_preferences_state')['rawPerformance']
    def state(**kw):return call('raw_window_state',**kw)
    def wait(full=False):
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            call('pump',waitMs=30);r=state()
            if r['ready'] and r['previewPresented']>0 and r['previewPresented']==r['previewRequested'] and (not full or r['previewFullResolution']):return r
        raise TimeoutError(r)
    if len(sys.argv)>1:
        call('init',settings=sys.argv[1]);call('window');call('window_scene',width=300,height=200,layers=[]);print(json.dumps(performance()));call('shutdown');return
    results=[]
    def test(name,fn):
        try:fn();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(results[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-raw-pref-') as folder:
        call('init',settings=folder);call('window');call('window_scene',width=300,height=200,layers=[])
        try:
            def transactions():
                defaults=performance();assert defaults==dict(backend=0,denoise=False,highQuality=True,amount=50,preview=1280,drag=640,delay=250,memoryMB=512,diskMB=4096),defaults
                values=dict(rawDefaultBackendPreference=1,rawDefaultDenoisePreference=False,rawHighQualityPreference=False,rawDenoiseAmountPreference=70,rawPreviewEdgePreference=960,rawDragEdgePreference=480,rawRefineDelayPreference=400,rawMemoryPreference=128,rawDiskPreference=2048)
                prefs(accept=False,**values);assert performance()==defaults
                prefs(accept=True,capture=str(OUT/'preferences.png'),**values)
                current=performance();assert current==dict(backend=1,denoise=False,highQuality=False,amount=70,preview=960,drag=480,delay=400,memoryMB=128,diskMB=2048),current
                child=subprocess.run([sys.executable,__file__,folder],capture_output=True,text=True,timeout=30,check=True)
                assert json.loads(child.stdout.strip().splitlines()[-1])==current,child.stdout
            test('defaults_cancel_accept_all_controls_new_process_persistence',transactions)
            def cache():
                pixels=[.2,.4,.8,1]*4
                call('raw_cache',memoryBudget=512,diskBudget=512,key='a',pixels=pixels)
                call('raw_cache',key='b',pixels=pixels);r=call('raw_cache',configureMemory=0,configureDisk=64,key='b');assert r['memory']==0 and r['disk']==64 and r['contains'],r
                assert not call('raw_cache',key='a')['contains']
                r=call('raw_cache',key='b',get=True);assert r['pixels']==pixels or max(abs(a-b) for a,b in zip(r['pixels'],pixels))<1e-6
                r=call('raw_cache',clear=True,key='b');assert r['memory']==r['disk']==0 and not r['contains']
            test('live_cache_budgets_evict_lru_disk_readback_clear',cache)
            def raw_defaults():
                call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),recipe={});r=wait()
                recipe=r['recipe'];assert recipe['demosaic']=='bilinear' and recipe['directml']==False and recipe['denoise']==False and recipe['denoiseAmount']==70,recipe
                assert r['previewEdge']==960 and r['cacheDiskBytes']>0 and r['cacheMemoryBytes']==0,r
            test('new_raw_real_default_decoder_device_amount_and_preview_budget',raw_defaults)
            def live():
                before=state()['recipe'];disk=state()['cacheDiskBytes']
                r=state(preferences=dict(accept=False,rawPreviewEdgePreference=800,clear=True));wait();assert performance()['preview']==960 and state()['cacheDiskBytes']==disk
                state(preferences=dict(accept=True,rawPreviewEdgePreference=800,rawMemoryPreference=1024,rawHighQualityPreference=True,rawDefaultBackendPreference=0,rawDenoiseAmountPreference=50,capture=str(OUT/'raw-gear.png')));r=wait()
                assert r['previewEdge']==800 and r['recipe']==before,(r['previewEdge'],r['recipe'],before)
                state(preferences=dict(accept=True,clear=True));r=wait();assert r['cacheDiskBytes']>0 and r['cacheMemoryBytes']>0 and r['recipe']==before
                state(click='rawZoom100');r=wait(full=True);assert r['previewFullResolution'] and r['previewEdge']==6000,r
                state(click='rawZoomFit');r=wait();assert r['previewEdge']==800,r
            test('actual_raw_gear_cancel_live_resize_clear_recompute_full_resolution_unchanged',live)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in results))
if __name__=='__main__':main()
