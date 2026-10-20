"""RAW WB numerical reference and actual C++/Qt source workflow."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/white-balance'

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
    def settings(t=6500,tint=0,mode='custom'):return dict(version=1,mode=mode,temperature=t,tint=tint)
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    xyz=np.array([[3.2404542,-1.5371385,-.4985314],[-.969266,1.8760108,.041556],[.0556434,-.2040259,1.0572252]])
    metadata=dict(xyzToCamera=xyz.ravel().tolist(),cameraToWorking=np.eye(3).ravel().tolist(),asShotGains=[1,1,1])
    with tempfile.TemporaryDirectory(prefix='huelith-wb-') as folder:
        call('init',settings=folder)
        try:
            def conversions():
                maximum=0
                for t in [2000,2500,2850,3200,4000,5000,5500,6500,7500,10000,20000,50000]:
                    for tint in [-100,-20,0,20,100]:
                        r=call('white_balance_probe',settings=settings(t,tint))
                        delta=abs(r['roundtrip']['temperature']-t)/t;maximum=max(maximum,delta)
                        assert delta<.015,(t,tint,r)
                        assert abs(r['roundtrip']['tint']-tint)<.5,(t,tint,r)
                d65=call('white_balance_probe',settings=settings(),xy=[.3127,.329])['converted']
                assert abs(d65['temperature']-6504)<40,d65
                (OUT/'conversion.json').write_text(json.dumps(dict(max_relative_roundtrip=maximum,d65=d65)),encoding='utf-8')
            test('temperature_tint_sixty_roundtrips_and_d65_reference',conversions)
            def matrices():
                for t in [2500,4000,6500,10000,25000]:
                    for tint in [-40,0,40]:
                        rgba=[-.05,.3,1.2,.5]
                        r=call('white_balance_probe',settings=settings(t,tint),metadata=metadata,rgba=rgba)
                        x,y=r['xy'];neutral=xyz@np.array([x/y,1,(1-x-y)/y]);neutral=np.clip(neutral/neutral.max(),.001,1);gains=neutral[1]/neutral
                        assert np.allclose(np.array(r['rgba'][:3]),np.array(rgba[:3])*gains,atol=3e-7,rtol=1e-7),r
                        assert r['rgba'][3]==.5
                r=call('white_balance_probe',settings=settings(mode='as-shot'),metadata=metadata,rgba=rgba)
                assert np.max(np.abs(np.array(r['rgba'])-rgba))<1e-7
            test('independent_numpy_camera_gain_matrix_hdr_negative_alpha_and_as_shot_identity',matrices)
            def neutral():
                for rgba in [[.3,.4,.5,1],[.5,.4,.3,1],[.2,.2,.2,.5]]:
                    sample=call('white_balance_probe',settings=settings(),metadata=metadata,rgba=rgba,sample=True)['sample']
                    r=call('white_balance_probe',settings=sample,metadata=metadata,rgba=rgba)['rgba']
                    assert max(r[:3])-min(r[:3])<.002,(rgba,sample,r)
                assert 'error' in raw('white_balance_probe',settings=settings(),metadata=metadata,rgba=[0,0,0,1],sample=True)
                assert 'error' in raw('white_balance_probe',settings=settings(),metadata=metadata,rgba=[1,1,1,1],sample=True)
            test('neutral_picker_removes_cast_and_rejects_black_or_clipped_samples',neutral)
            def invalid():
                for value in [settings(1999),settings(50001),settings(tint=151),settings(mode='unknown'),dict(version=7),dict(settings(),extra=True)]:
                    assert 'error' in raw('white_balance_probe',settings=value)
                assert 'error' in raw('white_balance_probe',settings=settings(),metadata={},rgba=[.1,.1,.1,1])
                assert 'error' in raw('white_balance_probe',settings=settings(),metadata=metadata,rgba=[.1,.1,.1,1],cancelled=True)
            test('invalid_version_parameters_metadata_and_cancel',invalid)
            def gui():
                call('window');path=str(ROOT/'research/m0/samples/r10-5871.CR3')
                call('raw_window',paths=[path,path],denoise=False)
                def wait():
                    deadline=time.monotonic()+120
                    while time.monotonic()<deadline:
                        call('pump',waitMs=40);s=call('raw_window_state')
                        if s['previewPresented']>0 and s['previewPresented']==s['previewRequested']:return s
                    raise TimeoutError(s)
                s=wait();assert len(s['recipe']['metadata']['cameraToWorking'])==9
                actual_metadata=s['recipe']['metadata']
                measured=call('white_balance_probe',settings=settings(mode='as-shot'),metadata=actual_metadata,rgba=[.2,.3,.4,1])
                manual=dict(measured['asShot'],mode='custom')
                reapplied=call('white_balance_probe',settings=manual,metadata=actual_metadata,rgba=[.2,.3,.4,1])['rgba']
                assert np.max(np.abs(np.array(reapplied)-[.2,.3,.4,1]))<.002,(manual,reapplied)
                s=call('raw_window_state',section=1,inventory=True)
                controls={c['name']:c for c in s['controls']}
                assert controls['rawWhiteBalanceMode']['enabled']
                assert len(controls['rawWhiteBalanceMode']['items'])==9
                for index,mode in [(2,'daylight'),(3,'cloudy'),(4,'shade'),(5,'tungsten'),(6,'fluorescent'),(7,'flash')]:
                    call('raw_window_state',combo='rawWhiteBalanceMode',comboIndex=index);s=wait()
                    assert s['recipe']['whiteBalance']['mode']==mode
                call('raw_window_state',control='rawWhiteBalanceTemperature',value=4300)
                call('raw_window_state',control='rawWhiteBalanceTint',value=12);s=wait();call('pump',waitMs=400)
                assert s['recipe']['whiteBalance']==settings(4300,12)
                call('raw_window_state',combo='rawWhiteBalanceMode',comboIndex=0);s=wait();call('pump',waitMs=400)
                assert s['recipe']['whiteBalance']['mode']=='as-shot'
                call('raw_window_state',click='rawUndo');s=wait();assert s['recipe']['whiteBalance']==settings(4300,12)
                call('raw_window_state',combo='rawWhiteBalanceMode',comboIndex=1);s=wait()
                assert s['recipe']['whiteBalance']['mode']=='auto'
                call('raw_window_state',click='rawWhiteBalancePicker')
                call('raw_window_state',stroke=[[.2,.6],[.2,.6]]);s=wait()
                assert s['recipe']['whiteBalance']['mode']=='custom'
                call('raw_window_state',escape=True)
                call('raw_window_state',widget='rawWhiteBalanceTemperatureSlider')
                perf=call('raw_slider_drag',slider='rawWhiteBalanceTemperatureSlider');wait()
                assert max(perf['eventMs'])<100 and perf['heartbeatTicks']>20,perf
                assert len(set(perf['presented']))>1,perf
                (OUT/'slider-response.json').write_text(json.dumps(perf),encoding='utf-8')
                s=call('raw_window_state',inventory=True,capture=str(OUT/'raw-white-balance.png'))
                assert not next(c for c in s['controls'] if c['name']=='rawWhiteBalancePicker')['checked']
                call('raw_window_state',click='rawSynchronize',sync=dict(groups=[1]));s=wait()
                assert s['recipes'][0]['whiteBalance']==s['recipes'][1]['whiteBalance']
                call('raw_window_state',index=1);wait()
                s=call('raw_window_state',accept=True);assert s['result']==1,s
                call('raw_window_state',save=str(Path(folder)/'wb.huelith'))
                (OUT/'raw-recipe.json').write_text(json.dumps(s['recipe'],indent=2),encoding='utf-8')
            test('actual_raw_presets_custom_auto_undo_sync_and_fullsize_project_roundtrip',gui)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
