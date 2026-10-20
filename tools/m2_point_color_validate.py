"""Point-color oracle and real RAW Qt interactions via production DLL."""
import colorsys
import ctypes
import json
import math
import os
from pathlib import Path
import random
import tempfile
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'docs/test-evidence/M2/point-color'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows'
    dll = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = dll.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p
    def raw(op, **kw):
        return json.loads(fn(json.dumps(dict(op=op, **kw)).encode()))
    def call(op, **kw):
        result = raw(op, **kw)
        assert 'error' not in result, result
        return result
    records = []
    def test(name, body):
        try:
            body()
            records.append(dict(name=name, passed=True))
            print('PASS', name, flush=True)
        except Exception:
            records.append(dict(name=name, passed=False, error=traceback.format_exc()))
            print(records[-1]['error'], flush=True)
    def wait():
        end = time.monotonic()+40
        while time.monotonic() < end:
            call('pump', waitMs=30)
            state = call('raw_window_state')
            if state['previewPresented'] > 0 and state['previewPresented'] == state['previewRequested']:
                return state
        raise AssertionError(state)
    def points(state):
        return state['recipe'].get('tone', {}).get('pointColors', [])
    with tempfile.TemporaryDirectory(prefix='huelith-point-') as folder:
        call('init', settings=folder)
        try:
            def identity():
                for rgb in ([0,0,0], [.2,.2,.2], [-.2,2,4], [.9,.1,.3]):
                    assert call('point_color_probe', rgb=rgb)['rgb'] == rgb
                    c = dict(sample=[0,1,.5])
                    assert call('point_color_probe', rgb=rgb, colors=[c])['rgb'] == rgb
            test('exact_identity_with_empty_or_zero_samples_hdr', identity)
            def oracle():
                rng = random.Random(752)
                for _ in range(100):
                    rgb = [rng.random() for _ in range(3)]
                    h,l,s = colorsys.rgb_to_hls(*rgb)
                    shift = [rng.uniform(-100,100) for _ in range(3)]
                    c = dict(sample=[h,s,l], shift=shift)
                    result = call('point_color_probe', rgb=rgb, colors=[c])
                    expected = colorsys.hls_to_rgb((h+shift[0]/600)%1,
                        l+shift[2]/100*(1-l if shift[2]>0 else l),
                        s+shift[1]/100*(1-s if shift[1]>0 else s))
                    assert max(abs(a-b) for a,b in zip(result['rgb'],expected)) < 2e-12
                    assert result['weights'] == [1]
            test('independent_colorsys_reference_100_selected_colors', oracle)
            def ranges():
                c = dict(sample=[0,1,.5], shift=[100,-50,20])
                assert call('point_color_probe',rgb=[0,1,0],colors=[c])['rgb'] == [0,1,0]
                assert call('point_color_probe',rgb=[.5]*3,colors=[c])['weights'] == [0]
                a=call('point_color_probe',rgb=colorsys.hls_to_rgb(.99,.5,1),colors=[c])
                b=call('point_color_probe',rgb=colorsys.hls_to_rgb(.01,.5,1),colors=[c])
                assert a['weights']==b['weights']==[1]
                for h in [.030001,.04,.05,.06,.079999]:
                    result=call('point_color_probe',rgb=colorsys.hls_to_rgb(h,.5,1),colors=[c])
                    t=(.08-h)/.05
                    assert abs(result['weights'][0]-t*t*(3-2*t))<1e-12
                for bad in ([dict(sample=[0,1,.5],hueRange=[0,.1,-.1,.2])], [c]*9,
                            [dict(sample=[0,1,.5],range=101)], [dict(sample=[0,1,.5],unknown=3)]):
                    assert 'error' in raw('point_color_probe',rgb=[1,0,0],colors=bad)
                c2=dict(sample=[0,1,.5],shift=[-50,10,-10])
                assert call('point_color_probe',rgb=[1,0,0],colors=[c,c2])['rgb']==call('point_color_probe',rgb=[1,0,0],colors=[c2,c])['rgb']
            test('circular_hue_falloff_gray_exclusion_invalid_ranges_order', ranges)
            def production():
                c=dict(sample=[0,1,.5],shift=[30,-20,10])
                state=call('tone_probe',settings=dict(version=4,pointColors=[c]),rgba=[.5,.02,.02,.5],save=str(Path(folder)/'point.huelith'))
                assert state['rgba'][3]==.5
                assert all(math.isfinite(v) for v in state['rgba'])
                off=call('tone_probe',settings=dict(version=4,pointColors=[c],sections=[True]*4+[False]+[True]*4),rgba=[.5,.02,.02,.5])
                assert max(abs(a-b) for a,b in zip(off['rgba'],[.5,.02,.02,.5]))<1e-6
            test('fp32_premultiplied_alpha_project_roundtrip_group_bypass', production)
            call('window')
            call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),denoise=False)
            wait()
            def gui():
                call('raw_window_state',click='rawSection0')
                call('raw_window_state',section=4,tab='rawMixerTabs',tabIndex=1)
                call('raw_window_state',click='rawPointPicker')
                wait()
                state=call('raw_window_state',stroke=[[.5,.5],[.5,.5]])
                assert len(points(state))==1,state
                wait()
                changed=call('raw_window_state',control='rawPointShift0',value=40)
                assert points(changed)[0]['shift']==[40,0,0]
                wait()
                undone=call('raw_window_state',click='rawUndo')
                assert points(undone)[0]['shift'][0]==0
                call('raw_window_state',click='rawRedo')
                wait()
                field=call('raw_window_state',widget='rawPointField',position=[.65,.3])
                assert abs(points(field)[0]['shift'][0]-30)<.1
                assert abs(points(field)[0]['shift'][1]-40)<.1
                light=call('raw_window_state',widget='rawPointLightField',position=[.5,.4])
                assert abs(points(light)[0]['shift'][2]-20)<.1
                range_before=points(light)[0]['hueRange']
                range_after=call('raw_window_state',widget='rawPointRange0',position=[.4,.5])
                assert points(range_after)[0]['hueRange']!=range_before
                assert sorted(points(range_after)[0]['hueRange'])==points(range_after)[0]['hueRange']
                call('raw_window_state',widget='rawPointField')
                wait()
                before=call('raw_window_state')['recipe']
                call('raw_window_state',click='rawPointVisualize')
                visual=wait()
                assert visual['recipe']==before
                call('raw_window_state',capture=str(OUT/'point-color-range.png'))
                call('raw_window_state',click='rawPointVisualize')
                wait()
                call('raw_window_state',click='rawPointPicker')
                assert call('raw_window_state',escape=True)['visible']
                wait()
                call('raw_window_state',capture=str(OUT/'point-color.png'))
                for count in range(2,9):
                    call('raw_window_state',click='rawPointPicker');wait()
                    state=call('raw_window_state',stroke=[[.5,.5],[.5,.5]])
                    assert len(points(state))==count
                    wait()
                call('raw_window_state',click='rawPointPicker');wait()
                state=call('raw_window_state',stroke=[[.5,.5],[.5,.5]])
                assert len(points(state))==8
                call('raw_window_state',escape=True);wait()
                state=call('raw_window_state',closeTab='rawPointSwatches',tabIndex=3)
                assert len(points(state))==7
                wait()
                assert len(points(call('raw_window_state',click='rawUndo')))==8
                wait()
                assert len(points(call('raw_window_state',click='rawRedo')))==7
                wait()
                result=call('raw_window_state',accept=True)
                assert result['result']==1,result
                call('raw_window_state',save=str(Path(folder)/'raw-point.huelith'))
            test('real_raw_pick_adjust_undo_redo_visualization_escape_save',gui)
            def synchronize():
                path=str(ROOT/'research/m0/samples/r10-5871.CR3')
                call('raw_window',paths=[path,path],denoise=False);wait()
                call('raw_window_state',section=4,tab='rawMixerTabs',tabIndex=1)
                call('raw_window_state',click='rawPointPicker');wait()
                call('raw_window_state',stroke=[[.5,.5],[.5,.5]]);wait()
                call('raw_window_state',control='rawPointShift0',value=25);wait()
                expected=points(call('raw_window_state'))
                call('raw_window_state',click='rawSynchronize',sync=dict(groups=[4]))
                state=call('raw_window_state',index=1);wait()
                assert state['recipes'][1]['tone']['pointColors']==expected
                state=call('raw_window_state',click='rawUndo');wait()
                assert not state['recipes'][1].get('tone',{}).get('pointColors',[])
                assert points(state)==expected
                call('raw_window_state',reject=True)
            test('point_color_multifile_mixer_only_sync_and_atomic_undo',synchronize)
        finally:
            call('shutdown')
    report=dict(passed=all(x['passed'] for x in records),checks=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False),flush=True)
    return 0 if report['passed'] else 1

if __name__=='__main__':
    raise SystemExit(main())
