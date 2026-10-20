"""RAW mask components: independent coverage arithmetic and production Qt gestures."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/raw-mask-components'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        if os.environ.get('HUELITH_MASK_TRACE') and op=='raw_window_state' and kw:print('STATE',kw,flush=True)
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r
        if os.environ.get('HUELITH_MASK_TRACE') and op=='raw_window_state' and kw:print('RETURN',r.get('status'),flush=True)
        return r
    def mask(components,**kw):return np.array(call('raw_mask_probe',local=dict(components=components),**kw)['mask'],dtype=float)
    def state(**kw):return call('raw_window_state',**kw)
    def wait():
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            call('pump',waitMs=30);r=state()
            if r['idle'] and r['previewPresented']>0 and r['previewPresented']==r['previewRequested']:return r
        raise TimeoutError(r)
    results=[]
    def test(name,body):
        try:body();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(results[-1],flush=True)
    a=dict(mode='radial',x=.1,y=.1,endX=.65,endY=.8,feather=.6)
    b=dict(mode='linear',x=.2,y=.5,endX=.85,endY=.5)
    def sets():
        A=mask([a]);B=mask([b]);
        for operation in ['add','subtract','intersect']:
            for invert in [False,True]:
                for density in [0,.3,1]:
                    C=np.floor(((255-B) if invert else B)*density+.5)
                    expected={'add':C+A*(255-C)/255,'subtract':A*(255-C)/255,'intersect':A*C/255}[operation]
                    actual=mask([a,dict(b,operation=operation,invert=invert,density=density)])
                    assert np.max(abs(actual-expected))<1.1,(operation,invert,density,np.max(abs(actual-expected)))
        assert np.array_equal(mask([a,dict(b,enabled=False)]),A)
        assert np.array_equal(mask([dict(a,invert=True)]),255-A)
    test('18_independent_set_operations_density_invert_disabled',sets)
    def ranges():
        linear=np.linspace(0,1,128);rgba=np.stack([linear,linear,linear,np.ones(128)],axis=1)
        component=dict(mode='luminance',low=.3,high=.6,smooth=.12)
        r=call('raw_mask_probe',width=128,height=1,rgba=rgba.ravel().tolist(),local=dict(components=[component]))
        rgb=np.array(r['display'])[:,:3]/255;lum=rgb@np.array([.2126,.7152,.0722]);t=np.clip(np.minimum((lum-.3)/.12+1,(.6-lum)/.12+1),0,1);expected=255*t*t*(3-2*t)
        assert np.max(abs(np.array(r['mask'])-expected))<.51
        half=rgba.copy();half[:,:3]*=.5;half[:,3]=.5
        r2=call('raw_mask_probe',width=128,height=1,rgba=half.ravel().tolist(),local=dict(components=[component]))
        rgb2=np.array(r2['display'])[:,:3]/255;lum2=rgb2@np.array([.2126,.7152,.0722]);t2=np.clip(np.minimum((lum2-.3)/.12+1,(.6-lum2)/.12+1),0,1)
        assert np.max(abs(np.array(r2['mask'])-255*t2*t2*(3-2*t2)))<.51,'partial alpha attenuated mask twice'
        assert np.max(abs(rgb2-rgb))<=1.01/255
        colors=np.array([[1,0,0,1],[0,1,0,1],[0,0,1,1],[.2,.2,.2,1]])
        encoded=call('raw_mask_probe',width=4,height=1,rgba=colors.ravel().tolist(),local={})['display']
        samples=[(np.array(encoded[i][:3])/255).tolist() for i in [0,2]]
        m=mask([dict(mode='color',range=0,colors=samples)],width=4,height=1,rgba=colors.ravel().tolist())
        assert m[0]>250 and m[2]>250 and m[1]<5 and m[3]<5,m
        assert np.max(mask([dict(mode='color',colors=[])]))==0
        for invalid in [dict(component,low=.9,high=.1),dict(b,operation='invalid'),dict(a,density=2),dict(mode='color',colors=[[.5,.5,.5]]*6),dict(mode='brush',flow=-1)]:
            r=json.loads(fn(json.dumps(dict(op='raw_mask_probe',local=dict(components=[invalid]))).encode()));assert 'error' in r,r
    test('luminance_smooth_reference_color_union_alpha_and_validation',ranges)
    def flow():
        brush=dict(mode='brush',radius=.2,feather=0,points=[dict(x=.5,y=.5)])
        full=mask([brush]);low=mask([dict(brush,flow=.25)]);dense=mask([dict(brush,flow=.25,density=.5)])
        assert 105<low.reshape(80,100)[40,50]<120
        assert np.max(abs(dense-low*.5))<=.51
        assert np.all(low<=full);assert np.max(mask([dict(brush,flow=0)]))==0
    test('brush_flow_accumulation_density_ceiling_zero',flow)
    with tempfile.TemporaryDirectory(prefix='huelith-mask-') as folder:
        call('init',settings=folder);call('window')
        try:
            call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),denoise=False);wait()
            def component(index=0):return state()['recipe']['locals'][0]['components'][index]
            def tree():
                state(click='rawRail2');state(click='rawAddLocal');state(mode=3,stroke=[[.2,.3],[.7,.6],[.7,.6]]);wait()
                state(maskItem=dict(local=0,component=0,name='gradient A',enabled=False))
                assert component()['name']=='gradient A' and not component()['enabled']
                state(maskItem=dict(local=0,component=0,enabled=True));state(click='rawMaskDuplicate');assert len(state()['recipe']['locals'][0]['components'])==2
                state(click='rawMaskDeleteComponent');assert len(state()['recipe']['locals'][0]['components'])==1
                state(click='rawUndo');assert len(state()['recipe']['locals'][0]['components'])==2
                state(click='rawRedo');assert len(state()['recipe']['locals'][0]['components'])==1
                state(maskItem=dict(local=0,component=0));state(click='rawMaskInvert');call('pump',waitMs=550);assert component()['invert']
                state(click='rawUndo');assert not component().get('invert',False)
            test('real_tree_rename_toggle_duplicate_delete_invert_undo_redo',tree)
            def handles_test():
                state(maskItem=dict(local=0,component=0));wait();before=component()
                state(stroke=[[.7,.6],[.8,.7],[.8,.7]],strokeHoldMs=550);after=component()
                assert abs(after['endX']-.8)<.004 and abs(after['x']-before['x'])<.001,(before,after)
                state(click='rawUndo');assert component()==before
                state(maskItem=dict(local=0,component=0));wait()
                state(stroke=[[.7,.6],[.8,.7],[.8,.7]],strokeEscape=True,strokeHoldMs=550);assert component()==before
                state(stroke=[[.45,.45],[.5,.5],[.5,.5]]);after=component();assert abs(after['x']-before['x']-.05)<.004
                state(click='rawUndo');assert component()==before
            test('actual_gradient_endpoint_center_long_drag_single_undo_escape',handles_test)
            def range_ui():
                state(maskItem=dict(local=0));state(click='rawAddLuminanceRange');state(control='rawMask_low',value=80);state(control='rawMask_high',value=40)
                c=component(1);assert c['low']==c['high']==.4
                inventory=state(inventory=True)['controls'];assert next(i for i in inventory if i['name']=='rawMask_low')['value']==40
                state(combo='rawMaskOperation',comboIndex=1);assert component(1)['operation']=='subtract'
                state(click='rawAddColorRange');wait();state(stroke=[[.25,.3],[.25,.3]]);wait();first=component(2)['colors'];assert len(first)==1
                state(stroke=[[.7,.6],[.7,.6]],modifiers=33554432);wait();assert len(component(2)['colors'])==2
                state(widget='rawMaskColorSample1',position=[.5,.5],modifiers=134217728);wait();assert len(component(2)['colors'])==1
                state(click='rawUndo');wait();assert len(component(2)['colors'])==2
                state(maskItem=dict(local=0,component=2));state(click='rawMaskColorPicker');state(escape=True);state(exposure=.5);wait();assert component(2)['colors'][0]==first[0]
                state(widget='rawMaskTree');state(capture=str(OUT/'range-components.png'))
            test('range_controls_crossed_bounds_color_picker_shift_samples_stability',range_ui)
            def brush_ui():
                state(maskItem=dict(local=0));state(mode=1,stroke=[[.2,.7],[.3,.7],[.3,.7]]);state(control='rawLocalDiameter',value=300);state(control='rawLocalFeather',value=25);state(control='rawMask_flow',value=40);state(control='rawMask_density',value=60);call('pump',waitMs=550)
                c=component(3);assert abs(c['radius']-.0375)<1e-8 and c['feather']==.25 and c['flow']==.4 and c['density']==.6,c
                wait();state(stroke=[[.2,.7],[.3,.8],[.3,.8]],modifiers=67108864);d=component(3);assert abs(d['points'][0]['x']-c['points'][0]['x']-.1)<.004
                state(click='rawUndo');assert component(3)==c
            test('brush_component_size_feather_flow_density_ctrl_pin_move',brush_ui)
            def ranges_and_preview():
                state(maskItem=dict(local=0,component=1));before=component(1);state(click='rawMaskColorPicker');wait()
                state(stroke=[[.2,.2],[.7,.7],[.7,.7]]);wait();after=component(1)
                assert 0<=after['low']<after['high']<=1 and after!=before
                state(click='rawUndo');assert component(1)==before
                state(maskItem=dict(local=0,component=2));state(combo='rawMaskOverlayMode',comboIndex=1);wait()
                state(previewOutput=str(OUT/'monochrome-'));pixels=np.array(Image.open(OUT/'monochrome-rawPreview.png'))
                assert np.array_equal(pixels[:,:,0],pixels[:,:,1]) and np.array_equal(pixels[:,:,1],pixels[:,:,2]) and pixels[:,:,0].std()>0
                recipe=state()['recipe'];state(combo='rawMaskOverlayMode',comboIndex=2);wait();state(previewOutput=str(OUT/'black-'))
                state(combo='rawMaskOverlayMode',comboIndex=3);wait();state(previewOutput=str(OUT/'white-'))
                dark=np.array(Image.open(OUT/'black-rawPreview.png')).astype(int);light=np.array(Image.open(OUT/'white-rawPreview.png')).astype(int)
                assert np.min(light[:,:,:3]-dark[:,:,:3])>=0 and np.max(light[:,:,:3]-dark[:,:,:3])>200
                assert state()['recipe']==recipe,'overlay accidentally changed develop recipe'
                state(combo='rawMaskOverlayMode',comboIndex=0);wait()
            test('luminance_rectangle_sample_single_undo_and_four_overlay_views',ranges_and_preview)
            def multi_local_workflow():
                # A real multi-mask edit: a long brush on one local adjustment,
                # a separate radial adjustment, then switch targets and undo.
                state(click='rawAddLocal');state(exposure=.8)
                call('pump',waitMs=350)
                points=[[.15+i*.006,.5+.1*np.sin(i*.15)] for i in range(100)]
                state(mode=1,stroke=points+[points[-1]],strokeHoldMs=700)
                s=wait();first=s['recipe']['locals'][1]
                assert first['tone']['exposure']==.8 and first['components']
                state(click='rawAddLocal');state(exposure=-.6)
                call('pump',waitMs=350)
                state(mode=4,stroke=[[.3,.3],[.65,.7],[.65,.7]])
                s=wait();assert len(s['recipe']['locals'])==3
                assert s['recipe']['locals'][1]==first
                state(maskItem=dict(local=1));state(exposure=1.2)
                call('pump',waitMs=350);state(click='rawUndo')
                s=wait();assert s['recipe']['locals'][1]==first
                assert s['recipe']['locals'][2]['tone']['exposure']==-.6
            test('multi_local_long_brush_radial_target_switch_and_independent_undo',multi_local_workflow)
            def persistence():
                wait();state(accept=True);state(save=str(Path(folder)/'masks.huelith'))
            test('all_component_parameters_production_project_roundtrip',persistence)
        finally:call('shutdown')
    report=dict(passed=sum(t['passed'] for t in results),total=len(results),tests=results)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
