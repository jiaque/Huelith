"""RAW crop acceptance through production Qt viewport, never a test executable."""
import ctypes, json, math, os, tempfile, time, traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/raw-crop'
def main():
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    lib=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=lib.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
        if 'error' in r:raise RuntimeError(r['error'])
        return r
    def state(**kw):return call('raw_window_state',**kw)
    def wait():
        end=time.monotonic()+120
        while time.monotonic()<end:
            call('pump',waitMs=20);s=state()
            if s['ready'] and s['previewPresented'] and s['previewPresented']==s['previewRequested']:return s
        raise TimeoutError(s)
    def rect():
        c=state()['recipe']['crop'];return [c['x'],c['y'],c['endX'],c['endY']]
    def ratio():
        x,y,r,b=rect();return (r-x)*6000/((b-y)*4000)
    results=[];OUT.mkdir(parents=True,exist_ok=True)
    def test(name,body):
        try:body();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(json.dumps(results[-1],ensure_ascii=False),flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-raw-crop-') as folder:
        call('init',settings=folder);call('window')
        call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),denoise=False)
        wait();state(click='rawRail1');wait()
        def free():
            state(stroke=[[.2,.2],[.8,.8]]);wait()
            assert max(abs(a-b) for a,b in zip(rect(),[.2,.2,.8,.8]))<.003,rect()
            before=rect();state(stroke=[[.5,.5],[.6,.55]]);wait()
            after=rect();assert abs(after[0]-before[0]-.1)<.004 and abs(after[1]-before[1]-.05)<.004,(before,after)
            state(click='rawUndo');wait();assert rect()==before
            state(click='rawRedo');wait();assert rect()==after
            state(stroke=[[after[2],after[3]],[.8,.7]]);wait()
            assert abs(rect()[2]-.8)<.004 and abs(rect()[3]-.7)<.004,rect()
        test('free_crop_move_corner_resize_undo_redo',free)
        def presets():
            for index,expected in [(2,1),(3,4/5),(4,5/7),(5,2/3),(6,16/9),(1,1.5)]:
                state(combo='rawCropRatio',comboIndex=index);wait()
                assert abs(ratio()-expected)<.006,(index,ratio())
            state(click='rawCropSwap');wait();assert abs(ratio()-2/3)<.006,ratio()
            state(control='rawCropWidth',value=7);state(control='rawCropHeight',value=3);wait()
            assert abs(ratio()-7/3)<.008,ratio()
            x,y,r,b=rect();state(stroke=[[r,b],[min(.98,r+.1),min(.98,b+.15)]]);wait()
            assert abs(ratio()-7/3)<.01,ratio()
            assert all(0<=v<=1 for v in rect())
            state(capture=str(OUT/'crop-ratio-handles.png'))
        test('six_presets_swap_custom_locked_corner_bounds',presets)
        def sides():
            state(click='rawResetCrop');wait();state(combo='rawCropRatio',comboIndex=2);wait()
            x,y,r,b=rect();state(stroke=[[(x+r)/2,y],[(x+r)/2,y+.1]]);wait()
            assert abs(ratio()-1)<.008,rect()
            state(click='rawCropLock');wait()
            before=rect();x,y,r,b=before
            state(stroke=[[r,(y+b)/2],[r-.08,(y+b)/2]]);wait()
            assert abs(rect()[1]-y)<.003 and abs(rect()[3]-b)<.003,(before,rect())
        test('locked_top_and_unlocked_right_edge_handles',sides)
        def handles_and_modifiers():
            for index in range(8):
                state(combo='rawCropRatio',comboIndex=0);state(click='rawResetCrop');wait()
                state(stroke=[[.2,.2],[.8,.8]]);wait()
                x,y,r,b=rect();cx,cy=(x+r)/2,(y+b)/2
                points=[(x,y),(cx,y),(r,y),(r,cy),(r,b),(cx,b),(x,b),(x,cy)]
                start=points[index];end=[start[0]+(.04 if start[0]<cx else -.04 if start[0]>cx else 0),
                                        start[1]+(.04 if start[1]<cy else -.04 if start[1]>cy else 0)]
                state(stroke=[start,end]);wait();actual=rect()
                expected=[x+(.04 if index in (0,6,7) else 0),y+(.04 if index in (0,1,2) else 0),
                          r-(.04 if index in (2,3,4) else 0),b-(.04 if index in (4,5,6) else 0)]
                assert max(abs(a-bb) for a,bb in zip(actual,expected))<.005,(index,actual,expected)
            state(combo='rawCropRatio',comboIndex=2);wait()
            x,y,r,b=rect();center=((x+r)/2,(y+b)/2)
            state(stroke=[[r,b],[r-.04,b-.04]],modifiers=0x08000000);wait()
            x,y,r,b=rect();assert abs((x+r)/2-center[0])<.003 and abs((y+b)/2-center[1])<.003
            assert abs(ratio()-1)<.008
            state(stroke=[[r,b],[r-.05,b-.01]],modifiers=0x02000000);wait()
            assert abs(ratio()-1)>.03,ratio()
            # Locked corner cannot displace its opposite anchor at a boundary.
            state(combo='rawCropRatio',comboIndex=6);wait()
            x,y,r,b=rect();state(stroke=[[r,b],[1,1]]);wait()
            assert abs(rect()[0]-x)<.003 and abs(rect()[1]-y)<.003,rect()
        test('all_eight_handles_alt_center_shift_override_boundary_anchor',handles_and_modifiers)
        def angle_control():
            state(control='rawCropAngle',value=12.25);wait();call('pump',waitMs=450)
            assert state()['recipe']['geometry']['rotation']==12.25
            state(click='rawUndo');wait();assert abs(state()['recipe'].get('geometry',{}).get('rotation',0))<1e-8
            state(click='rawRedo');wait();assert state()['recipe']['geometry']['rotation']==12.25
            state(control='rawCropAngle',value=0);wait();call('pump',waitMs=450)
        test('crop_angle_control_shared_geometry_history',angle_control)
        def cancel():
            before=state()['recipe'];state(stroke=[[.03,.03]])
            state(escape=True);wait()
            assert state()['visible'] and state()['recipe']==before
        test('escape_cancels_only_current_gesture',cancel)
        def outside_rotation():
            state(combo='rawCropRatio',comboIndex=0);state(click='rawResetCrop');wait()
            state(stroke=[[.2,.2],[.8,.8]]);wait();before=state()['recipe']
            state(stroke=[[.9,.5],[.85,.8]],modifiers=0x02000000);wait()
            angle=state()['recipe']['geometry']['rotation'];assert abs(angle-30)<.01,angle
            assert state()['recipe']['crop']==before['crop']
            state(click='rawUndo');wait();assert state()['recipe']==before
            state(click='rawRedo');wait();assert state()['recipe']['geometry']['rotation']==angle
            state(stroke=[[.9,.5]]);state(escape=True);wait();assert state()['recipe']['geometry']['rotation']==angle
            state(control='rawCropAngle',value=0);wait();call('pump',waitMs=450)
        test('outside_crop_rotation_shift_snap_single_undo_escape',outside_rotation)
        def quarter_turns():
            for turns in range(4):
                core=call('raw_local_probe',recipe=dict(quarterTurns=turns),points=[])
                assert (core['width'],core['height'])==((80,100) if turns%2 else (100,80)),core
            initial=state()['recipe']
            state(click='rawRotateRight');wait();assert state()['recipe']['quarterTurns']==1
            assert abs(state()['navigation'][0]['rotation']-90)<1e-6
            state(click='rawZoom100');wait();assert abs(state()['scale']-1)<1e-9
            state(click='rawZoomTool');state(nav=dict(drag=[[.5,.5],[.5,.5]]));wait()
            assert abs(state()['scale']-2)<1e-6
            assert abs(state()['navigation'][0]['rotation']-90)<1e-6
            state(click='rawUndo');wait();assert state()['recipe']==initial
            state(click='rawRedo');wait();assert state()['recipe']['quarterTurns']==1
            state(click='rawZoomFit');state(click='rawRail1');wait()
            state(combo='rawCropRatio',comboIndex=3);wait();assert abs(1/ratio()-4/5)<.01,ratio()
            state(click='rawRotateLeft');wait();assert state()['recipe']['quarterTurns']==0
            state(capture=str(OUT/'rotation-controls.png'))
        test('four_orientation_dimensions_view_transform_zoom_history_and_rotated_ratio',quarter_turns)
        def straighten():
            state(click='rawCropStraighten');state(stroke=[[.3,.4],[.7,.5]]);wait()
            angle=state()['recipe']['geometry']['rotation']
            assert abs(angle+math.degrees(math.atan2(.1*4000,.4*6000)))<.35,angle
            state(click='rawUndo');wait();assert abs(state()['recipe'].get('geometry',{}).get('rotation',0))<1e-8
            state(click='rawRedo');wait();assert state()['recipe']['geometry']['rotation']==angle
            state(capture=str(OUT/'straightened.png'))
            state(click='rawRotateRight');wait()
            c=rect();expected_width=round((c[3]-c[1])*4000);expected_height=round((c[2]-c[0])*6000)
            s=state(accept=True);assert s['result']==1,s
            assert abs(s['outputWidth']-expected_width)<=2 and abs(s['outputHeight']-expected_height)<=2,(s['outputWidth'],s['outputHeight'],expected_width,expected_height)
            state(save=str(Path(folder)/'crop.huelith'))
        test('straighten_line_angle_undo_redo_full_commit_save',straighten)
        call('shutdown')
    report=dict(passed=sum(r['passed'] for r in results),total=len(results),tests=results)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
