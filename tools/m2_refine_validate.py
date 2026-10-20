"""Independent RGB guided-filter math, mask semantics, seams, alpha, and Qt workflows."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
from PIL import Image
from scipy.ndimage import uniform_filter,gaussian_filter,maximum_filter,minimum_filter
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/selection-refine'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert 'error' not in r,r;return r
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    def reference(rgb,p,r):
        box=lambda a:uniform_filter(a,size=2*r+1,mode='nearest')
        means=np.stack([box(rgb[:,:,c]) for c in range(3)],axis=-1);mp=box(p)
        covariance=np.empty((*p.shape,3,3));rhs=np.empty((*p.shape,3))
        for a in range(3):
            rhs[:,:,a]=box(rgb[:,:,a]*p)-means[:,:,a]*mp
            for b in range(3):covariance[:,:,a,b]=box(rgb[:,:,a]*rgb[:,:,b])-means[:,:,a]*means[:,:,b]+(.001 if a==b else 0)
        coefficients=np.linalg.solve(covariance,rhs[...,None])[...,0]
        intercept=mp-np.sum(coefficients*means,axis=-1)
        return np.clip(sum(box(coefficients[:,:,c])*rgb[:,:,c] for c in range(3))+box(intercept),0,1)
    with tempfile.TemporaryDirectory(prefix='huelith-refine-') as folder:
        folder=Path(folder);call('init',settings=str(folder/'settings'))
        def probe(rgb,mask,settings=None,area=None,**kw):
            Image.fromarray(rgb).save(folder/'input.png');Image.fromarray(mask).save(folder/'mask.png')
            if area is not None:Image.fromarray(area).save(folder/'area.png');kw['area']=str(folder/'area.png')
            r=call('refine_probe',image=str(folder/'input.png'),mask=str(folder/'mask.png'),settings=settings or {},output=str(folder/'output.png'),**kw)
            return np.array(Image.open(folder/'output.png')),r
        try:
            rng=np.random.default_rng(149)
            rgb=rng.integers(0,256,(33,47,3),dtype=np.uint8);mask=rng.integers(0,256,(33,47),dtype=np.uint8)
            def math():
                for radius in [1,4,9]:
                    actual,_=probe(rgb,mask,dict(radius=radius));expected=reference(rgb/255.,mask/255.,radius)*255
                    assert np.max(abs(actual-expected))<.55,np.max(abs(actual-expected))
                zero,_=probe(rgb,mask);assert np.array_equal(zero,mask)
                inverted,_=probe(rgb,mask,dict(invert=True));assert np.array_equal(inverted,255-mask)
            test('independent_scipy_numpy_rgb_guided_filter_and_zero_identity',math)
            def bounds():
                square=np.zeros((79,111),np.uint8);square[20:60,35:80]=255;image=np.zeros((*square.shape,3),np.uint8)+80
                for sign in [-1,1]:
                    actual,_=probe(image,square,dict(shift=sign));expected=(maximum_filter if sign>0 else minimum_filter)(square,size=3,mode='nearest');assert np.array_equal(actual,expected)
                feather,_=probe(image,square,dict(feather=2.4));assert np.any((feather>0)&(feather<255))
                contrast,_=probe(image,feather,dict(contrast=.7));assert np.count_nonzero((contrast>0)&(contrast<255))<np.count_nonzero((feather>0)&(feather<255))
                for v in [0,255]:
                    m=np.zeros(square.shape,np.uint8)+v;a,_=probe(image,m,dict(radius=5,smart=True,smooth=25,feather=6,shift=-.4));assert np.max(abs(a.astype(float)-v))<1
                for invalid in [dict(radius=-1),dict(radius=251),dict(feather=1001),dict(contrast=2),dict(shift=-2),dict(decontaminate=-1)]:
                    assert 'error' in raw('refine_probe',image=str(folder/'input.png'),mask=str(folder/'mask.png'),settings=invalid)
                assert 'error' in raw('refine_probe',image=str(folder/'input.png'),mask=str(folder/'mask.png'),cancelled=True)
            test('shift_hard_edges_feather_contrast_uniform_masks_invalid_cancel',bounds)
            def seams():
                h,w=540,1070;y,x=np.mgrid[:h,:w];image=np.stack([(x*3+y)%256,(x+y*2)%256,(x*2-y)%256],axis=-1).astype(np.uint8)
                m=np.round(np.clip(.5+.4*np.sin(x*.07)+.1*np.sin(y*.03),0,1)*255).astype(np.uint8)
                a,_=probe(image,m,dict(radius=7));expected=reference(image/255.,m/255.,7)*255
                assert np.max(abs(a-expected))<.56,np.max(abs(a-expected))
                area=np.zeros((h,w),np.uint8);area[:,:w//2]=255;b,_=probe(image,m,dict(radius=7),area=area)
                assert np.array_equal(b[:,w//2:],m[:,w//2:]);assert np.array_equal(b[:,:w//2],a[:,:w//2])
            test('full_reference_across_512_tile_boundaries_and_brushed_region',seams)
            def edges():
                h,w=160,240;y,x=np.mgrid[:h,:w];truth=(x<90+18*np.sin(y*.15)).astype(float)
                image=np.stack([truth*.8+.1,(1-truth)*.7+.1,np.zeros_like(truth)+.2],axis=-1)
                image=np.round(image*255).astype(np.uint8);rough=np.round(gaussian_filter(truth,3)*255).astype(np.uint8)
                guided,_=probe(image,rough,dict(radius=6));before=np.mean((rough/255.-truth)**2);guided_mse=np.mean((guided/255.-truth)**2)
                # The opaque contour needs Contrast as well as Radius; retain the
                # first radius-only quality result instead of claiming it met the target.
                a,r=probe(image,rough,dict(radius=6,contrast=.2));after=np.mean((a/255.-truth)**2)
                assert after<before*.5,(before,guided_mse,after)
                (OUT/'edge-quality.json').write_text(json.dumps(dict(before_mse=before,radius_only_mse=guided_mse,after_mse=after,settings=dict(radius=6,contrast=.2),milliseconds=r['ms']),indent=2),encoding='utf-8')
                Image.fromarray(image).save(OUT/'edge-source.png');Image.fromarray(rough).save(OUT/'edge-before.png');Image.fromarray(a).save(OUT/'edge-after.png')
            test('known_opaque_fine_contour_radius_and_contrast_error_reduction',edges)
            def decontaminate():
                image=np.zeros((40,80,4),np.uint8);image[:]=[0,170,170,128];image[:,:36]=[240,20,30,128]
                m=np.zeros((40,80),np.uint8);m[:,:35]=255;m[:,35:43]=128
                _,r=probe(image,m,dict(decontaminate=.00001),pixels=str(folder/'baseline.bin'));assert r['recolored']
                base=np.fromfile(folder/'baseline.bin',np.float32).reshape(40,80,4)
                _,r=probe(image,m,dict(decontaminate=1),pixels=str(folder/'recolored.bin'))
                output=np.fromfile(folder/'recolored.bin',np.float32).reshape(40,80,4)
                assert np.array_equal(base[:,:,3],output[:,:,3]);assert np.allclose(output[:,:,3],128/255,atol=1e-6)
                assert np.array_equal(base[:,:35],output[:,:35]);assert np.array_equal(base[:,43:],output[:,43:])
                assert np.linalg.norm(output[20,39,:3]-output[20,20,:3])<np.linalg.norm(base[20,39,:3]-base[20,20,:3])
                assert np.isfinite(output).all()
            test('color_decontamination_preserves_source_alpha_and_interior',decontaminate)
            def ready(committed=False):
                deadline=time.monotonic()+60
                while time.monotonic()<deadline:
                    call('pump',waitMs=20);r=call('refine_window')
                    assert not r['failed'],r
                    if (not r['visible'] and r['result']==1) if committed else r['ready']:return r
                raise TimeoutError(r)
            def fixture():
                y,x=np.mgrid[:240,:360];m=np.where((x-175)**2+(y-115)**2<70**2,255,0).astype(np.uint8)
                image=np.stack([np.where(m>0,220,50),np.where(m>0,80,130),np.full_like(m,60)],axis=-1).astype(np.uint8)
                Image.fromarray(image).save(folder/'ui.png');Image.fromarray(m).save(folder/'ui-mask.png')
                call('refine_window',image=str(folder/'ui.png'),mask=str(folder/'ui-mask.png'));ready();return m
            def ui():
                initial=fixture();call('refine_window',save=str(folder/'ui-before.png'))
                call('refine_window',refine_feather=2.5,refine_contrast=15);ready();call('pump',waitMs=380)
                s=call('refine_window');assert s['history']==2,s
                call('refine_window',key='Ctrl+Z');s=ready();assert s['controls']['refine_feather']==0,s
                call('refine_window',save=str(folder/'undo.png'));assert np.array_equal(np.array(Image.open(folder/'undo.png')),initial)
                call('refine_window',key='Ctrl+Shift+Z');s=ready();assert s['controls']['refine_feather']==2.5,s
                call('refine_window',refineDecontaminate=True);s=ready();assert s['controls']['refineOutput']==3 and s['outputEnabled'][:2]==[False,False],s
                call('refine_window',refineDecontaminate=False,refineViewMode=5,capture=str(OUT/'refine-black-white.png'));ready()
                for i in range(7):call('refine_window',refineViewMode=i,capture=str(OUT/f'view-{i}.png'))
                call('refine_window',refineRemember=True,accept=True);ready(True)
                call('refine_window',resultMask=str(folder/'committed.png'));assert Image.open(folder/'committed.png').size==(360,240)
                fixture();s=call('refine_window');assert s['controls']['refine_feather']==2.5 and s['controls']['refine_contrast']==15
                call('refine_window',refine_feather=7,reject=True);fixture();assert call('refine_window')['controls']['refine_feather']==2.5
                call('refine_window',refineRemember=False,click='refineReset');ready();call('refine_window',accept=True);ready(True)
            test('qt_controls_undo_redo_seven_views_output_guard_remember_cancel',ui)
            def brush():
                initial=fixture();call('refine_window',refineTool=1,refineBrushSize=30,stroke=[[.15,.1],[.15,.2],[.15,.3]])
                ready();call('refine_window',save=str(folder/'painted.png'));painted=np.array(Image.open(folder/'painted.png'));assert np.count_nonzero(painted)>np.count_nonzero(initial)
                call('refine_window',key='Ctrl+Z');ready();call('refine_window',save=str(folder/'undo-brush.png'));assert np.array_equal(np.array(Image.open(folder/'undo-brush.png')),initial)
                call('refine_window',stroke=[[.5,.3],[.5,.5],[.5,.7]],alt=True);ready();call('refine_window',save=str(folder/'erased.png'));assert np.count_nonzero(np.array(Image.open(folder/'erased.png')))<np.count_nonzero(initial)
                call('refine_window',key='Ctrl+Z');ready()
                call('refine_window',stroke=[[.2,.1],[.2,.2],[.2,.3]],escapeStroke=True);ready();call('refine_window',save=str(folder/'escaped.png'));assert np.array_equal(np.array(Image.open(folder/'escaped.png')),initial)
                call('refine_window',refineTool=2,stroke=[[.05,.05],[.2,.05],[.2,.2],[.05,.2],[.05,.05]]);ready();call('refine_window',save=str(folder/'lasso.png'));assert np.array(Image.open(folder/'lasso.png'))[25,25]>0
                call('refine_window',key='Ctrl+Z');ready();call('refine_window',refineTool=0,refineBrushSize=50,stroke=[[.65,.15],[.68,.3],[.68,.5]])
                ready();call('refine_window',capture=str(OUT/'refine-brushed.png'));call('refine_window',reject=True)
            test('real_brush_alt_erase_lasso_refine_brush_escape_single_history',brush)
            def views():
                original=fixture();call('refine_window',reject=True)
                back=np.zeros((240,360,4),np.uint8);back[:]=[20,60,210,255]
                Image.fromarray(back).save(folder/'background.png')
                call('refine_window',image=str(folder/'ui.png'),mask=str(folder/'ui-mask.png'),background=str(folder/'background.png'));ready()
                call('refine_window',refineViewMode=6,display=str(folder/'layers-view.png'))
                actual=np.array(Image.open(folder/'layers-view.png').convert('RGB'))
                assert np.array_equal(actual[10,10],[20,60,210]),actual[10,10]
                assert np.array_equal(actual[115,175],[220,80,60]),actual[115,175]
                call('refine_window',refine_radius=5);ready()
                call('refine_window',refineShowEdge=True,display=str(folder/'edge-band.png'))
                band=np.array(Image.open(folder/'edge-band.png').convert('L'))
                expected=(maximum_filter(original,size=11,mode='nearest')>minimum_filter(original,size=11,mode='nearest'))*255
                assert np.array_equal(band,expected)
                call('refine_window',reject=True)
            test('on_layers_actual_background_and_independent_edge_radius_band',views)
            def outputs():
                call('refine_window',close=True);call('window')
                for mode in range(6):
                    call('window_open',path=str(folder/'ui.png'));call('window_key',key='M');call('window_gesture',x=95,y=35,endX=255,endY=190)
                    before=call('window_probe')['history'];count=call('window_state')['count']
                    call('window_action',text='选择并遮住…',dialog=dict(refine_feather=2,refineOutput=mode,accept=True))
                    s=call('window_probe')
                    if mode>=4:assert call('window_state')['count']==count+1
                    else:assert s['history']==before+1,(mode,s)
                    if mode in [1,3,5]:assert s['layers'][-1]['masked'],(mode,s)
                    if mode in [2,3]:assert len(s['layers'])==2
                    if mode==3:
                        call('window_design',file=str(folder/'refined.huelith'))
                        call('window_open',path=str(folder/'refined.huelith'));assert call('window_probe')['layers'][-1]['masked']
                    if mode<4 and mode!=3:call('window_key',key='Ctrl+Z');assert call('window_probe')['history']==before
            test('main_menu_six_outputs_history_new_document_project_mask_roundtrip',outputs)
            def transformed_masks():
                for warped in [False,True]:
                    call('window_scene',width=400,height=300,layers=[dict(name='subject',color='red',width=200,height=160,x=100,y=70)])
                    if warped:
                        call('window_key',key='V');call('window_control',name='warpMode',value=True)
                        a=call('window_mouse',phase='press',warp=4)
                        call('window_mouse',phase='move',space='widget',x=a['x'],y=a['y']+25)
                        call('window_mouse',phase='release',space='widget',x=a['x'],y=a['y']+25)
                        call('window_key',key='Return');call('window_control',name='warpMode',value=False)
                    call('window_key',key='M');call('window_gesture',x=140,y=110,endX=230,endY=175)
                    call('window_action',text='选择并遮住…',dialog=dict(refineOutput=1,accept=True))
                    assert call('window_probe',x=170,y=140)['rgba'][3]>250
                    assert call('window_probe',x=270,y=140)['rgba'][3]<5
                    call('window_design',render=str(folder/'masked-before.png'))
                    call('window_action',text='选择并遮住…',dialog=dict(refineOutput=1,accept=True))
                    call('window_design',render=str(folder/'masked-after.png'))
                    a=np.array(Image.open(folder/'masked-before.png'));b=np.array(Image.open(folder/'masked-after.png'))
                    assert np.mean(abs(a.astype(float)-b))<.5,(warped,np.mean(abs(a.astype(float)-b)))
                    call('window_key',key='V');call('window_gesture',x=170,y=140,endX=190,endY=160)
                    assert call('window_probe',x=190,y=160)['rgba'][3]>250
                    assert call('window_probe',x=145,y=115)['rgba'][3]<5
                call('window_scene',width=400,height=300,layers=[dict(name='group',kind=5,x=60,y=40,children=[dict(name='child',color='red',width=180,height=150,x=40,y=30)])])
                call('window_key',key='V');call('window_gesture',x=150,y=120,endX=150,endY=120)
                s=call('window_probe');assert next(l for l in s['layers'] if l['id']==s['active'])['name']=='child'
                call('window_key',key='M');call('window_gesture',x=120,y=90,endX=200,endY=140)
                call('window_action',text='选择并遮住…',dialog=dict(refineOutput=1,accept=True))
                assert call('window_probe',x=150,y=120)['rgba'][3]>250
                assert call('window_probe',x=250,y=120)['rgba'][3]<5
            test('transformed_warped_linked_nested_masks_coordinate_roundtrip',transformed_masks)
            def raw_refine():
                call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),denoise=False)
                def state(**kw):return call('raw_window_state',**kw)
                def idle():
                    deadline=time.monotonic()+120
                    while time.monotonic()<deadline:
                        call('pump',waitMs=30);r=state()
                        if r['idle'] and r['previewPresented']>0 and r['previewPresented']==r['previewRequested']:return r
                    raise TimeoutError(r)
                idle();state(click='rawRail2');state(click='rawAddLocal')
                state(mode=2,stroke=[[.2,.2],[.7,.7],[.7,.7]]);state(exposure=.5);idle();call('pump',waitMs=550)
                before=state()['recipe']['locals']
                state(click='rawMaskRefine',refinement=dict(refine_feather=3,accept=False));assert state()['recipe']['locals']==before
                state(click='rawMaskRefine',refinement=dict(refine_feather=3,accept=True));idle()
                after=state()['recipe']['locals'];assert after[0]['tone']==before[0]['tone'] and after[0].get('name')==before[0].get('name')
                c=after[0]['components'];assert len(c)==1 and c[0]['mode']=='bitmap' and c[0]['png']
                state(click='rawUndo');assert state()['recipe']['locals']==before
                state(click='rawRedo');assert state()['recipe']['locals']==after
                state(accept=True)
            test('raw_real_modal_refinement_cancel_one_undo_preserve_tone',raw_refine)
            def large():
                image=Image.new('RGB',(6000,4000),(120,170,90));image.save(folder/'large.png')
                from PIL import ImageDraw
                mask=Image.new('L',image.size);ImageDraw.Draw(mask).ellipse((1300,700,4700,3300),fill=255);mask.save(folder/'large-mask.png')
                call('refine_window',image=str(folder/'large.png'),mask=str(folder/'large-mask.png'));ready()
                timings=[]
                for i in range(12):
                    start=time.perf_counter();call('refine_window',refine_radius=i+1,refine_feather=i*.2);timings.append((time.perf_counter()-start)*1000)
                    call('pump',waitMs=25)
                ready();start=time.perf_counter();call('refine_window',accept=True);ready(True);elapsed=time.perf_counter()-start
                call('refine_window',resultMask=str(folder/'large-result.png'))
                assert Image.open(folder/'large-result.png').size==image.size
                assert max(timings)<150,(max(timings),timings)
                (OUT/'24mp-performance.json').write_text(json.dumps(dict(input_ms=timings,full_commit_seconds=elapsed,size=list(image.size))),encoding='utf-8')
            test('24mp_real_async_preview_input_latency_full_resolution_commit',large)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))

if __name__=='__main__':main()
