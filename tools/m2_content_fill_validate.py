"""Exemplar fill: independent texture/source restrictions and actual async Qt output transactions."""
import base64,ctypes,json,os,tempfile,traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/content-fill'
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
    def b64(a):return base64.b64encode(a.tobytes()).decode()
    def request(image,mask,**kw):return dict(patch=True,contentFill=True,width=image.shape[1],height=image.shape[0],data=b64(image.astype('<f4')),mask=b64(mask.astype('uint8')),**kw)
    def probe(image,mask,**kw):
        r=call('brush_probe',**request(image,mask,**kw));return np.frombuffer(base64.b64decode(r['data']),dtype='<f4').reshape(image.shape),r
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-fill-') as folder:
        call('init',settings=folder)
        try:
            y,x=np.mgrid[:128,:160];base=np.ones((128,160,4),np.float32)
            pattern=((x//4+y//4)%2)*.08
            for c in range(3):base[:,:,c]=.15+c*.04+pattern
            mask=np.zeros((128,160),np.uint8);mask[45:73,60:92]=255
            dirty=base.copy();dirty[mask>0,:3]=.95
            def texture():
                filled,r=probe(dirty,mask,adaptation=0,candidates=768,capture=str(OUT/'texture-fill.png'))
                error=np.sqrt(np.mean((filled[mask>0,:3]-base[mask>0,:3])**2));assert error<.025,error
                assert np.array_equal(filled[mask==0],dirty[mask==0])
                again,_=probe(dirty,mask,adaptation=0,candidates=768);assert np.array_equal(filled,again)
                assert filled[mask>0,:3].max()<.4
                print(dict(texture_rmse=float(error),core_ms=r['elapsedMs']),flush=True)
            test('texture_reconstruction_determinism_no_object_feedback_and_outside_identity',texture)
            def restriction():
                image=np.ones((128,160,4),np.float32);image[:,:,:3]=[.8,.1,.1];image[:,:55,:3]=[.1,.7,.2]
                allowed=np.zeros((128,160),np.uint8);allowed[:,:45]=255
                filled,_=probe(image,mask,allowed=b64(allowed),adaptation=0)
                assert np.allclose(filled[mask>0,:3],[.1,.7,.2])
                assert 'error' in raw('brush_probe',**request(image,mask,allowed=b64(np.zeros_like(mask))))
                full=np.full_like(mask,255);assert 'error' in raw('brush_probe',**request(image,full))
                assert 'error' in raw('brush_probe',**request(image,mask,cancelled=True))
            test('allowed_source_mask_exclusion_insufficient_sources_and_cancel',restriction)
            def alpha_hdr():
                image=np.ones_like(base);image[:,:,:3]=[2,.8,.3];image[mask>0]=0
                soft=mask.copy();soft[45:48,60:92]=128
                filled,_=probe(image,soft,adaptation=0,apply=True)
                assert np.allclose(filled[soft==255],[2,.8,.3,1])
                assert np.allclose(filled[soft==128],np.array([2,.8,.3,1])*128/255,atol=1e-6)
                assert np.array_equal(filled[soft==0],image[soft==0])
            test('hdr_and_transparent_holes_soft_coverage_applied_once',alpha_hdr)
            def gradient():
                image=base.copy();image[:,:,:3]+=x[:,:,None]*.001+y[:,:,None]*.0005
                broken=image.copy();broken[mask>0,:3]=.9
                fixed,r=probe(broken,mask,adaptation=.8,candidates=384)
                before=np.mean(abs(broken[mask>0,:3]-image[mask>0,:3]));after=np.mean(abs(fixed[mask>0,:3]-image[mask>0,:3]))
                assert after<before*.1,(before,after)
            test('color_adaptation_on_sloping_illumination',gradient)
            call('window')
            rgb=np.round(base[:,:,:3]*255).astype('uint8');rgb[mask>0]=245
            path=Path(folder)/'defect.png';Image.fromarray(rgb).save(path)
            def scene(width=160,height=128,path=path):
                call('window_scene',width=width,height=height,layers=[dict(name='source',path=str(path))]);call('window_control',name='toolButton_2',value=True)
                call('window_gesture',x=60,y=45,endX=92,endY=73)
            def transformed_sources():
                rng=np.random.default_rng(8724);errors=[]
                for mode,options in [('rotation',dict(rotation=4)),('mirror',dict(mirror=True)),('scale',dict(scale=True)),('rotatedMirror',dict(rotation=4,mirror=True))]:
                    size=21 if mode=='scale' else 15
                    donor=np.ones((size,size,4),np.float32);donor[:,:,:3]=rng.uniform(.05,.8,(size,size,3))
                    target=donor[::2,::2] if mode=='scale' else np.fliplr(donor) if mode=='mirror' else np.rot90(np.fliplr(donor) if mode=='rotatedMirror' else donor)
                    image=np.ones((42,70,4),np.float32)*.1;image[:,:,3]=1
                    image[3:3+size,3:3+size]=donor
                    h,w=target.shape[:2];tx,ty=40,14
                    image[ty:ty+h,tx:tx+w]=target
                    mask=np.zeros(image.shape[:2],np.uint8);cx,cy=tx+w//2,ty+h//2;mask[cy-1:cy+2,cx-1:cx+2]=255
                    expected=image.copy();image[mask>0,:3]=.95
                    allowed=np.zeros_like(mask);allowed[3:3+size,3:3+size]=255
                    common=dict(allowed=b64(allowed),radius=4,candidates=2048,adaptation=0)
                    plain,_=probe(image,mask,**common)
                    filled,_=probe(image,mask,**common,**options)
                    error=float(np.max(abs(filled[mask>0]-expected[mask>0])))
                    plain_error=float(np.mean(abs(plain[mask>0]-expected[mask>0])))
                    assert error<2e-6,(mode,error,plain_error)
                    assert plain_error>.01,(mode,plain_error)
                    assert np.array_equal(filled[mask==0],image[mask==0])
                    errors.append(dict(mode=mode,maxError=error,unadaptedMeanError=plain_error))
                assert 'error' in raw('brush_probe',**request(image,mask,rotation=5))
                (OUT/'adaptation-errors.json').write_text(json.dumps(errors,indent=2),'utf-8')
            test('independent_rotated_scaled_mirrored_source_oracles_and_protection',transformed_sources)
            def dialogs():
                scene();before=call('window_brush');layers=len(call('window_design')['layers'])
                cancelled=call('window_brush',contentDialog=True,accept=False,capture=str(OUT/'fill-workspace.png'))
                assert cancelled['content']['ready'] and cancelled['history']==before['history'] and cancelled['hash']==before['hash'],cancelled['content']
                early=call('window_brush',contentDialog=True,cancelEarly=True);assert early['hash']==before['hash']
                for output in [0,1,2]:
                    scene();before=call('window_brush')
                    r=call('window_brush',contentDialog=True,fillOutput=output,accept=True,pixels=str(OUT/f'output-{output}.png'))
                    assert r['content']['ready'] and r['history']==before['history']+1 and r['hash']!=before['hash'],r['content']
                    assert len(call('window_design')['layers'])==layers+(output!=1)
                    image=np.asarray(Image.open(OUT/f'output-{output}.png'))
                    assert image[50:65,65:85,:3].mean()<150,image[55,70]
                    assert np.array_equal(image[10:25,10:30,:3],rgb[10:25,10:30])
                    call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==before['hash']
                    call('window_key',key='Ctrl+Shift+Z');assert call('window_brush')['hash']==r['hash']
                call('window_design',file=str(OUT/'content-fill.huelith'));call('window_open',path=str(OUT/'content-fill.huelith'));assert call('window_brush')['hash']==r['hash']
            test('actual_async_preview_cancel_early_cancel_three_outputs_undo_redo_save',dialogs)
            def sampling_ui():
                scene();before=call('window_brush')
                r=call('window_brush',contentDialog=True,fillAllSampling=True,samplingPoints=[[.1,.15],[.2,.15]],undoSampling=True,accept=False)
                hashes=r['content']['samplingHashes'];assert len(hashes)==3 and hashes[0]!=hashes[1] and hashes[0]==hashes[2],r['content']
                r=call('window_brush',contentDialog=True,fillSamplingMode=2,fillSubtract=False,fillBrushSize=60,samplingPoints=[[.18,.5],[.25,.5]],accept=True,capture=str(OUT/'custom-sampling.png'))
                assert r['content']['ready'] and r['history']==before['history']+1,r['content']
                assert r['content']['samplingHashes'][0]!=r['content']['samplingHashes'][1]
                call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==before['hash']
            test('actual_sampling_brush_subtract_add_custom_source_and_local_undo',sampling_ui)
            def adaptation_ui():
                scene();before=call('window_brush')
                result=call('window_brush',contentDialog=True,fillRotationAdaptation=4,fillScale=True,fillMirror=True,fillSampleAllLayers=True,accept=True,capture=str(OUT/'adaptation-options.png'))
                assert result['content']['ready'] and result['history']==before['history']+1
                call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==before['hash']
                hashes=[]
                for sample_all in [False,True]:
                    call('window_scene',width=160,height=128,layers=[dict(name='active blue',color='#2840b4'),dict(name='upper red overlay',color='#cc3030',opacity=.5)])
                    call('window_select',index=1)
                    call('window_control',name='toolButton_2',value=True);call('window_gesture',x=63,y=47,endX=91,endY=72)
                    r=call('window_brush',contentDialog=True,fillSampleAllLayers=sample_all,fillAllSampling=True,accept=True)
                    assert r['content']['ready'],r['content'];hashes.append(r['hash'])
                assert hashes[0]!=hashes[1]
            test('actual_adaptation_options_all_layers_source_scope_and_single_undo',adaptation_ui)

            def full_resolution():
                large=Image.fromarray(rgb).resize((1000,800),Image.Resampling.NEAREST)
                large_path=Path(folder)/'large.png';large.save(large_path)
                call('window_scene',width=1000,height=800,layers=[dict(name='large',path=str(large_path))]);call('window_control',name='toolButton_2',value=True)
                call('window_gesture',x=375,y=281,endX=575,endY=456)
                before=call('window_brush')
                r=call('window_brush',contentDialog=True,fillOutput=0,accept=True,pixels=str(OUT/'full-result.png'))
                assert r['content']['ready'] and r['history']==before['history']+1,r['content']
                image=np.asarray(Image.open(OUT/'full-result.png'));assert image.shape[:2]==(800,1000)
                assert image[300:430,400:550,:3].mean()<160
            test('proxy_preview_then_full_resolution_background_commit',full_resolution)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
