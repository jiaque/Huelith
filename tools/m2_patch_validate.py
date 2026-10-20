"""Independent linear-float repair fixtures plus actual Patch tool mouse transactions."""
import base64, ctypes, json, os, tempfile, traceback
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/patch'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        value=raw(op,**kw);assert 'error' not in value,value;return value
    def encode(a):return base64.b64encode(a.tobytes()).decode()
    def probe(image,mask,**kw):
        r=call('brush_probe',patch=True,width=image.shape[1],height=image.shape[0],data=encode(image.astype('<f4')),mask=encode(mask.astype('uint8')),**kw)
        return np.frombuffer(base64.b64decode(r['data']),dtype='<f4').reshape(image.shape),r
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-patch-') as tmp:
        call('init',settings=tmp)
        try:
            y,x=np.mgrid[:96,:160];base=np.ones((96,160,4),np.float32)
            for c in range(3):base[:,:,c]=.1+.0015*x+.0008*y+c*.025+((x%8)<4)*.04
            mask=np.zeros((96,160),np.uint8);mask[30:54,42:66]=255
            dirty=base.copy();dirty[mask>0,:3]=.9
            def harmonic():
                fixed,r=probe(dirty,mask,dx=64,apply=True,capture=str(OUT/'gradient-repaired.png'))
                assert np.max(abs(fixed-base))<3e-4,np.max(abs(fixed-base))
                assert np.array_equal(fixed[mask==0],dirty[mask==0])
                large=np.ones((640,768,4),np.float32);yy,xx=np.mgrid[:640,:768]
                large[:,:,:3]=(.1+xx*.0005+yy*.0002)[:,:,None]
                m=np.zeros((640,768),np.uint8);m[30:605,30:210]=255
                d=large.copy();d[m>0,:3]=.95
                repaired,r=probe(d,m,dx=400)
                assert np.max(abs(repaired-large))<.002,(np.max(abs(repaired-large)),r['elapsedMs'])
                print('large patch ms',r['elapsedMs'],flush=True)
            test('harmonic_color_match_texture_preservation_large_roi_and_unchanged_outside',harmonic)
            def soft_and_holes():
                m=mask.copy();m[35:40,50:55]=0;m[30:32,42:66]=128
                fixed,_=probe(dirty,m,dx=64,apply=True,adaptation=0)
                expected=dirty.copy();weight=m[:,:,None]/255.
                shifted=dirty.copy();shifted[:,:96]=dirty[:,64:]
                expected=dirty*(1-weight)+shifted*weight
                assert np.max(abs(fixed-expected))<1e-6,np.max(abs(fixed-expected))
                assert np.array_equal(fixed[m==0],dirty[m==0])
                same,_=probe(dirty,mask,dx=999,apply=True);assert np.array_equal(same,dirty)
                transparent=dirty.copy();transparent[:,100:,:]=0
                fixed,_=probe(transparent,mask,dx=64,apply=True)
                assert np.isfinite(fixed).all() and np.array_equal(fixed[mask>0],transparent[mask>0])
            test('soft_mask_applied_once_holes_bounds_and_transparent_sources',soft_and_holes)
            def destination():
                fixed,_=probe(base,mask,dx=64,destination=True,apply=True,adaptation=0)
                expected=base.copy();expected[30:54,106:130]=base[30:54,42:66]
                assert np.max(abs(fixed-expected))<1e-6
                repair,_=probe(dirty,mask,dx=64,repair=True,apply=True,file=str(OUT/'repair-layer.huelith'))
                assert np.max(abs(repair-base))<3e-4
                assert np.array_equal(repair[mask==0],dirty[mask==0])
            test('source_destination_independent_repair_layer_and_project_roundtrip',destination)
            def automatic():
                repaired,r=probe(dirty,mask,automatic=True,apply=True)
                assert abs(r['dx'])+abs(r['dy'])>0
                before=np.mean(abs(dirty[mask>0,:3]-base[mask>0,:3]));after=np.mean(abs(repaired[mask>0,:3]-base[mask>0,:3]))
                assert after<before*.15,(before,after,r['dx'],r['dy'])
                request=dict(patch=True,width=160,height=96,data=encode(dirty),mask=encode(mask),dx=64,cancelled=True)
                assert 'error' in raw('brush_probe',**request)
            test('automatic_whole_patch_search_and_cancel',automatic)
            call('window')
            yy,xx=np.mgrid[:320,:512];rgb=np.empty((320,512,3),np.uint8)
            for c in range(3):rgb[:,:,c]=np.clip(60+xx*.12+yy*.04+c*12+((xx%20)<10)*12,0,255)
            rgb[140:180,170:210]=245
            path=Path(tmp)/'defect.png';Image.fromarray(rgb).save(path)
            def scene():
                call('window_scene',width=512,height=320,layers=[dict(name='source',path=str(path)),dict(name='repair',kind=2)])
                call('window_control',name='toolButton_27',value=True)
            def mouse(phase,x,y):call('window_mouse',phase=phase,x=x,y=y)
            def lasso():
                mouse('press',168,138)
                for x,y in [(212,138),(212,182),(168,182)]:mouse('move',x,y)
                mouse('release',168,138)
            def ui():
                scene();before=call('window_brush');lasso();selected=call('window_brush')
                assert selected['history']==before['history']+1
                assert call('window_interaction_state')['rangeCount']>1600
                mouse('press',190,160);mouse('move',310,160)
                call('window_capture',path=str(OUT/'patch-preview.png'))
                call('window_key',key='Esc');assert call('window_brush')['hash']==selected['hash'] and call('window_brush')['history']==selected['history']
                call('window_gesture',x=190,y=160,endX=310,endY=160)
                r=call('window_brush',pixels=str(OUT/'patch-committed.png'))
                assert r['history']==selected['history']+1 and r['hash']!=selected['hash']
                actual=np.asarray(Image.open(OUT/'patch-committed.png'))[:,:,:3]
                assert actual[160,190].mean()<190,actual[160,190]
                assert np.array_equal(actual[20:100,20:100],rgb[20:100,20:100])
                call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==selected['hash']
                call('window_key',key='Ctrl+Shift+Z');assert call('window_brush')['hash']==r['hash']
                call('window_design',file=str(OUT/'patch-ui.huelith'));call('window_open',path=str(OUT/'patch-ui.huelith'))
                assert call('window_brush')['hash']==r['hash']
            test('actual_lasso_source_drag_live_preview_escape_commit_undo_redo_reopen',ui)
            def destination_ui():
                scene();lasso();call('window_brush',patchDestination=True)
                call('window_control',name='patchAdaptation',value=0)
                call('window_gesture',x=190,y=160,endX=310,endY=160)
                call('window_brush',pixels=str(OUT/'patch-destination.png'))
                actual=np.asarray(Image.open(OUT/'patch-destination.png'))
                assert actual[160,310,:3].mean()>240,actual[160,310]
                assert call('window_interaction_state')['rangeCount']>1600
            test('actual_destination_radio_moves_copy_and_selection',destination_ui)
            def healing_brushes():
                for tool in [17,18]:
                    scene();call('window_control',name=f'toolButton_{tool}',value=True)
                    call('window_brush',settings=dict(version=1,diameter=68,hardness=.8,pressureSize=False))
                    if tool==17:
                        call('window_mouse',phase='press',x=310,y=160,modifiers=134217728)
                        call('window_mouse',phase='release',x=310,y=160,modifiers=134217728)
                    before=call('window_brush')
                    call('window_gesture',x=190,y=160,endX=190,endY=160)
                    r=call('window_brush',pixels=str(OUT/f'brush-{tool}.png'))
                    assert r['history']==before['history']+1 and r['hash']!=before['hash']
                    actual=np.asarray(Image.open(OUT/f'brush-{tool}.png'))
                    assert actual[155:165,185:195,:3].mean()<190,(tool,actual[160,190])
                    assert np.array_equal(actual[20:100,20:100,:3],rgb[20:100,20:100])
                    call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==before['hash']
            test('actual_healing_and_spot_brushes_sample_below_preserve_outside_and_undo',healing_brushes)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
