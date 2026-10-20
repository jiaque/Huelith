"""RAW profile pipeline, independent matrix reference and real browser/multiphoto/auto UI."""
import ctypes,json,os,tempfile,time,traceback,math
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/raw-profiles'
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))];os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert 'error' not in r,r;return r
    def state(**kw):return call('raw_window_state',**kw)
    def wait():
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            call('pump',waitMs=30);r=state()
            if r['idle'] and not r['profiles']['busy'] and r['previewPresented']>0 and r['previewPresented']==r['previewRequested']:return r
        raise TimeoutError(r)
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-profiles-') as folder:
        call('init',settings=folder);call('window')
        try:
            profiles=call('raw_profile_probe',list=True)['profiles']
            def pixels():
                rng=np.random.default_rng(351);a=rng.uniform(-.15,2,(80,4));a[:,3]=rng.uniform(.1,1,80);a[:,:3]*=a[:,3,None];values=a.astype(np.float32).astype(float)
                def probe(profile):return np.array(call('raw_profile_probe',pixels=values.ravel().tolist(),recipe=dict(profile=profile))['pixels']).reshape(-1,4)
                assert np.array_equal(probe(profiles[0]),values)
                original=probe(dict(profiles[7],amount=0));full=probe(profiles[7]);double=probe(dict(profiles[7],amount=200))
                np.testing.assert_allclose(double[:,:3],2*full[:,:3]-original[:,:3],atol=5e-7,rtol=1e-6)
                for p in profiles:
                    out=probe(p);assert np.isfinite(out).all();np.testing.assert_array_equal(out[:,3],values[:,3])
                assert np.max(abs(probe(profiles[5])[:,:3]-values[:,:3]))>.01
                for p in [dict(profiles[0],id='huelith.fake'),dict(profiles[7],amount=-1),dict(profiles[7],amount=201),dict(id='custom',version=1,name='bad',matrix1=[0]*9,camera='X')]:assert 'error' in raw('raw_profile_probe',pixels=[.1,.2,.3,1],recipe=dict(profile=p))
                assert 'error' in raw('raw_profile_probe',pixels=[.1,.2,.3,1],recipe=dict(profile=profiles[1]),cancelled=True)
            test('eight_profiles_hdr_alpha_identity_strength_and_invalid_inputs',pixels)
            def matrices():
                s=np.array([[3.2404542,-1.5371385,-.4985314],[-.969266,1.8760108,.041556],[.0556434,-.2040259,1.0572252]])
                r=np.array([[1.716651188,-.355670784,-.253366281],[-.666684352,1.616481237,.015768546],[.017639857,-.042770613,.942103121]])
                m1=np.array([[.8,-.1,-.1],[-.2,1.1,.1],[.1,-.2,.9]]);m2=np.array([[.9,-.2,-.1],[-.15,1.05,.1],[.05,-.1,.95]])
                old=np.array([[.9,.1,0],[.05,.9,.05],[0,.1,.9]]);g=np.array([2.,1.,1.5]);rgba=np.array([[.1,.4,.2,.5],[1.1,.1,-.1,1.]],dtype=np.float32)
                base=dict(id='test.camera',name='Dual',version=1,camera='Test',matrix1=m1.ravel().tolist(),matrix2=m2.ravel().tolist(),temperature1=2850,temperature2=6500,tone=profiles[0]['tone'])
                meta=dict(camera='Test',cameraToWorking=old.ravel().tolist(),asShotGains=g.tolist())
                errors=[]
                for temp in [2000,2850,4000,5000,6500,10000]:
                    t=np.clip((1/temp-1/2850)/(1/6500-1/2850),0,1);camera=(1-t)*m1+t*m2;n=camera@np.linalg.inv(s);n/=n.sum(axis=1)[:,None]
                    correction=r@np.linalg.inv(s)@np.linalg.inv(n)@np.diag(g)@np.linalg.inv(old)
                    expected=rgba[:,:3]@correction.T
                    result=np.array(call('raw_profile_probe',pixels=rgba.ravel().tolist(),recipe=dict(profile=base,metadata=meta,whiteBalance=dict(version=1,mode='custom',temperature=temp,tint=0)))['pixels']).reshape(-1,4)
                    errors.append(float(abs(result[:,:3]-expected).max()));np.testing.assert_allclose(result[:,:3],expected,atol=2e-7,rtol=2e-6);np.testing.assert_array_equal(result[:,3],rgba[:,3])
                assert 'error' in raw('raw_profile_probe',pixels=rgba.ravel().tolist(),recipe=dict(profile=base,metadata=dict(meta,camera='Other')))
                (OUT/'matrix-reference.json').write_text(json.dumps(dict(max_errors=errors)),encoding='utf-8')
            test('dual_illuminant_reciprocal_temperature_matrix_numpy_reference_camera_guard',matrices)
            def automatic():
                for level in [.002,.02,.18,1,4]:
                    p=[level]*3+[1];r=call('raw_profile_probe',pixels=p*100,tone=dict(version=8,saturation=31,texture=12))['automatic']
                    assert -5<=r['exposure']<=5 and r['saturation']==31 and r['texture']==12
                    expected=np.clip(min(math.log2(.18/level),math.log2(.95/level)+1),-5,5)
                    assert abs(r['exposure']-expected)<1e-6
                assert call('raw_profile_probe',pixels=[0,0,0,0],tone=dict(version=8,exposure=2))['automatic']['exposure']==2
            test('automatic_tone_scene_referred_quantile_reference_and_unrelated_parameter_preservation',automatic)
            call('raw_window',paths=[str(ROOT/'research/m0/samples/r10-5871.CR3')]*2,denoise=False);wait()
            def browser():
                state(exposure=.6);wait();call('pump',waitMs=400);before=state()['recipes'][0]['tone']
                state(click='rawProfileBrowse');deadline=time.monotonic()+15
                while time.monotonic()<deadline:
                    call('pump',waitMs=30);items=state()['profiles']['items']
                    if all(i['thumbnail'] for i in items):break
                assert len(items)==8 and all(i['thumbnail'] for i in items),items
                state(profileUI=dict(id='huelith.landscape'));wait();r=state();assert r['recipes'][0]['profile']['id']=='huelith.landscape' and r['recipes'][0]['tone']==before
                state(click='rawProfileFavorite');state(combo='rawProfileFilter',comboIndex=1);assert [i['id'] for i in state()['profiles']['items']]==['huelith.landscape']
                state(combo='rawProfileFilter',comboIndex=0);assert all(i['thumbnail'] for i in state()['profiles']['items']);state(capture=str(OUT/'browser.png'))
                state(profileUI=dict(id='huelith.vintage'));wait();state(control='rawProfileAmount',value=40);wait();call('pump',waitMs=400)
                assert state()['recipes'][0]['profile']['amount']==40
                state(click='rawUndo');wait();assert state()['recipes'][0]['profile']['amount']==100
                state(click='rawRedo');wait();assert state()['recipes'][0]['profile']['amount']==40
                state(click='rawProfileBack');state(click='rawMonochrome');wait();assert state()['recipes'][0]['profile']['id']=='huelith.monochrome'
                state(click='rawMonochrome');wait();assert state()['recipes'][0]['profile']['id']=='huelith.color'
            test('actual_thumbnail_browser_favorites_profile_independent_sliders_amount_undo_bw',browser)
            def imported():
                state(click='rawProfileBrowse');meta=state()['recipes'][0]['metadata']
                profile=dict(type='huelith.raw-profile',id='test.r10',version=1,name='R10 calibrated',camera=meta['camera'],matrix1=meta['xyzToCamera'],tone=profiles[4]['tone'])
                path=Path(folder)/'camera.hrawprofile';path.write_text(json.dumps(profile),encoding='utf-8');state(profileUI={'import':str(path)})
                assert any(x['id']=='test.r10' for x in state()['profiles']['items'])
                state(profileUI=dict(id='test.r10'));wait();assert state()['recipes'][0]['profile']['id']=='test.r10'
                recipe=state()['recipes'][0];result=call('raw_recipe_probe',source=recipe,target={},groups=[1]);preset=result['preset'];assert preset['recipe']['profile']['matrix1']==profile['matrix1']
                assert call('raw_recipe_probe',data=json.dumps(preset))['preset']==preset
                other=dict(profile,id='test.other',camera='Wrong camera');path.write_text(json.dumps(other),encoding='utf-8');state(profileUI={'import':str(path)})
                assert all(x['id']!='test.other' for x in state()['profiles']['items'])
                state(click='rawProfileBack')
            test('custom_camera_profile_actual_file_import_camera_filter_portable_preset',imported)
            def multi_auto():
                state(index=1);wait();state(exposure=-1);state(control='toneSaturation',value=30);wait();call('pump',waitMs=400)
                state(film=dict(row=0,modifiers=67108864));wait();state(combo='rawProfileChoice',text='Huelith 风景');wait();r=state()
                assert all(x['profile']['id']=='huelith.landscape' for x in r['recipes'])
                before=r['recipes'];start=time.perf_counter();state(click='rawAutoTone');elapsed=(time.perf_counter()-start)*1000;wait();after=state()['recipes']
                assert elapsed<150,elapsed
                assert abs(after[0]['tone']['exposure']-after[1]['tone']['exposure'])<1e-6
                assert after[1]['tone']['saturation']==30 and after[0]['tone']['saturation']!=30
                state(click='rawUndo');wait();assert [x['tone'] for x in state()['recipes']]==[x['tone'] for x in before]
                state(click='rawRedo');wait();assert [x['tone'] for x in state()['recipes']]==[x['tone'] for x in after]
                (OUT/'auto-performance.json').write_text(json.dumps(dict(input_ms=elapsed)),encoding='utf-8')
                state(accept=True);state(save=str(Path(folder)/'profiles.huelith'))
            test('multi_photo_profile_auto_background_atomic_history_project_roundtrip',multi_auto)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8');raise SystemExit(not all(x['passed'] for x in records))
if __name__=='__main__':main()

