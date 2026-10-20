"""Production detail pipeline: independent Gaussian oracle, noise quality, Qt and persistence."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/details'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        result=raw(op,**kw);assert 'error' not in result,result;return result
    def probe(a,amount=0,**settings):
        result=call('detail_probe',width=a.shape[1],height=a.shape[0],pixels=a.ravel().tolist(),
                    amount=amount,settings=dict(version=1,**settings))
        return np.array(result['pixels']).reshape(a.shape)
    def rgba(rgb,alpha=1):
        a=np.empty((*rgb.shape[:2],4),np.float32);a[:,:,:3]=rgb*alpha;a[:,:,3]=alpha;return a
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-details-') as folder:
        call('init',settings=folder)
        try:
            def gaussian():
                y,x=np.mgrid[:24,:32];g=.3+.1*np.sin(x*.6)+.04*np.cos(y*.4)
                a=rgba(np.repeat(g[:,:,None],3,2))
                for radius in (.5,1,2.2,3):
                    reach=int(np.ceil(2*radius));k=np.exp(-np.arange(-reach,reach+1)**2/(2*radius**2));k/=k.sum()
                    ref=g.copy()
                    for axis in (1,0):
                        ref=np.apply_along_axis(lambda row:np.convolve(np.pad(row,reach,mode='edge'),k,mode='valid'),axis,ref)
                    expected=g+.8*(g-ref)
                    actual=probe(a,80,radius=radius,detail=100)
                    assert np.max(np.abs(actual[:,:,:3]-expected[:,:,None]))<2e-6
            test('independent_gaussian_unsharp_reference_four_radii',gaussian)
            def invariant():
                a=rgba(np.full((20,30,3),[-.1,2,4]),.5)
                assert np.array_equal(probe(a),a)
                assert np.max(np.abs(probe(a,150,luminance=100,color=100)-a))<2e-6
                a[:,:8,:3]=0;a[:,:8,3]=0
                result=probe(a,150,luminance=100,color=100)
                assert np.array_equal(result[:,:,3],a[:,:,3])
                assert np.max(np.abs(result[:,8:,:3]-a[:,8:,:3]))<2e-6
            test('zero_identity_constant_hdr_negative_alpha_no_transparent_edge_bleed',invariant)
            def quality():
                rng=np.random.default_rng(760)
                truth=np.full((96,128),.25);truth[:,64:]=.75
                noisy=truth+rng.normal(0,.035,truth.shape)
                a=rgba(np.repeat(noisy[:,:,None],3,2))
                filtered=probe(a,luminance=100,luminanceDetail=20)[:,:,0]
                before=np.mean((noisy-truth)**2);after=np.mean((filtered-truth)**2)
                assert after<before*.65,(before,after)
                assert filtered[:,66:70].mean()-filtered[:,58:62].mean()>.45
                masked=probe(a,100,masking=100)[:,:,0]
                unmasked=probe(a,100,masking=0)[:,:,0]
                assert np.var(masked[:,8:50])<np.var(unmasked[:,8:50])
                mask=call('detail_probe',width=128,height=96,pixels=a.ravel().tolist(),
                          amount=100,settings=dict(version=1,masking=100),preview=1)
                mask=np.array(mask['pixels']).reshape(a.shape)
                assert np.array_equal(mask[:,:,0],mask[:,:,1])
                assert np.array_equal(mask[:,:,1],mask[:,:,2])
                assert mask[:,63:65,0].mean()>mask[:,8:50,0].mean()+.5
                chroma=rng.normal(0,.05,(96,128,2))
                rgb=np.repeat(truth[:,:,None],3,2)
                rgb[:,:,0]+=chroma[:,:,0];rgb[:,:,2]+=chroma[:,:,1]
                rgb[:,:,1]-=(.2126*chroma[:,:,0]+.0722*chroma[:,:,1])/.7152
                b=rgba(rgb);smooth=probe(b,color=100,colorDetail=20,colorSmoothness=80)
                cb=np.mean((rgb-truth[:,:,None])**2);ca=np.mean((smooth[:,:,:3]-truth[:,:,None])**2)
                assert ca<cb*.65,(cb,ca)
                weights=np.array([.2126,.7152,.0722])
                assert np.max(np.abs(smooth[:,:,:3]@weights-b[:,:,:3]@weights))<2e-7
                (OUT/'quality.json').write_text(json.dumps(dict(luminance_before_mse=before,luminance_after_mse=after,color_before_mse=cb,color_after_mse=ca)),encoding='utf-8')
            test('noise_mse_reduction_contrast_boundary_masking_chroma_luma_preservation',quality)
            def invalid():
                a=rgba(np.ones((4,4,3)))
                base=dict(width=4,height=4,pixels=a.ravel().tolist())
                for settings in (dict(radius=0),dict(detail=101),dict(luminance=-1),dict(colorDetail='x'),dict(unknown=1)):
                    assert 'error' in raw('detail_probe',**base,settings=dict(version=1,**settings))
                assert 'error' in raw('detail_probe',**base,cancelled=True)
                for version in (1,2,3,4,5):
                    call('tone_probe',rgba=[.1,.2,.3,.5],settings=dict(version=version),save=str(Path(folder)/f'v{version}.huelith'))
            test('invalid_parameters_cancellation_legacy_versions_project_roundtrip',invalid)
            def gui():
                call('window')
                path=str(ROOT/'research/m0/samples/r10-5871.CR3')
                call('raw_window',paths=[path,path],denoise=False)
                def wait():
                    deadline=time.monotonic()+120
                    while time.monotonic()<deadline:
                        call('pump',waitMs=40);s=call('raw_window_state')
                        if s['previewPresented']>0 and s['previewPresented']==s['previewRequested']:return s
                    raise TimeoutError(s)
                wait()
                s=call('raw_window_state',section=7,inventory=True)
                call('raw_window_state',click='rawSection0')
                controls={c['name']:c for c in s['controls']}
                assert not controls['toneNoiseLuminanceDetail']['enabled']
                assert not controls['toneNoiseColorDetail']['enabled']
                values={'toneSharpening':75,'toneSharpenRadius':1.7,'toneSharpenDetail':45,
                        'toneSharpenMasking':35,'toneNoiseLuminance':35,'toneNoiseLuminanceDetail':65,
                        'toneNoiseLuminanceContrast':20,'toneNoiseColor':40,'toneNoiseColorDetail':60,
                        'toneNoiseColorSmoothness':70}
                for name,value in values.items():
                    call('raw_window_state',control=name,value=value)
                wait();call('pump',waitMs=400)
                s=call('raw_window_state',widget='toneNoiseColorSmoothness',inventory=True,capture=str(OUT/'detail-panel.png'))
                detail=s['recipe']['tone']['details']
                assert detail['radius']==1.7 and detail['colorSmoothness']==70
                controls={c['name']:c for c in s['controls']}
                assert controls['toneNoiseLuminanceDetail']['enabled'] and controls['toneNoiseColorDetail']['enabled']
                s=call('raw_window_state',widget='toneSharpenMaskingSlider',position=[.6,.5],
                       mousePhase='press',modifiers=0x08000000)
                assert s['detailPreview']==1
                wait();call('raw_window_state',capture=str(OUT/'mask-alt-preview.png'))
                s=call('raw_window_state',widget='toneSharpenMaskingSlider',position=[.6,.5],mousePhase='release')
                assert s['detailPreview']==0
                wait();call('pump',waitMs=400)
                call('raw_window_state',control='toneNoiseColorSmoothness',value=20);wait();call('pump',waitMs=400)
                s=call('raw_window_state',click='rawUndo');assert s['recipe']['tone']['details']['colorSmoothness']==70
                call('raw_window_state',click='rawRedo');wait()
                call('raw_window_state',click='rawSynchronize',sync=dict(groups=[6]));wait()
                s=call('raw_window_state');assert s['recipes'][1]['tone']['details']==s['recipes'][0]['tone']['details']
                call('raw_window_state',index=1);wait()
                start=time.monotonic();s=call('raw_window_state',accept=True)
                assert s['result']==1,s
                call('raw_window_state',save=str(Path(folder)/'details.huelith'))
                (OUT/'commit.json').write_text(json.dumps(dict(seconds=time.monotonic()-start,recipe=s['recipe'])),encoding='utf-8')
            test('real_raw_all_controls_dependencies_undo_sync_full_resolution_commit_save',gui)
        finally:call('shutdown')
    report=dict(passed=all(r['passed'] for r in records),checks=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
