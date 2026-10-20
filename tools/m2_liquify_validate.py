"""Production liquify: geometry/alpha/HDR, protection, persistence and actual Qt transactions."""
import base64,ctypes,json,os,tempfile,traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/liquify'
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
    def request(image,**kw):return dict(liquify=True,width=image.shape[1],height=image.shape[0],data=base64.b64encode(image.astype('<f4').tobytes()).decode(),**kw)
    def probe(image,**kw):
        r=call('brush_probe',**request(image,**kw));return np.frombuffer(base64.b64decode(r['data']),dtype='<f4').reshape(image.shape),r
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-liquify-') as folder:
        call('init',settings=folder)
        try:
            y,x=np.mgrid[:128,:160];base=np.ones((128,160,4),np.float32);base[:,:,0]=x/160;base[:,:,1]=y/128;base[:,:,2]=2
            warp=dict(tool=0,size=70,density=.6,pressure=1,**{'from':[65,64],'to':[80,64]})
            def geometry():
                identity,_=probe(base);assert np.array_equal(identity,base)
                moved,r=probe(base,dabs=[warp]);assert abs(moved[64,80,0]-65/160)<1e-6,moved[64,80]
                assert np.array_equal(moved[:20],base[:20]);assert np.all(moved[:,:,2]==2)
                assert r['elapsedMs']<1000,r
                off,_=probe(base,dabs=[warp],enabled=False);assert np.array_equal(off,base)
                half,_=probe(base,dabs=[warp],amount=.5);assert np.allclose(half,(moved+base)/2)
                saved,r=probe(base,dabs=[warp],file=str(OUT/'mesh.huelith'));assert r['roundtrip']
                print({'core_ms':r['elapsedMs']},flush=True)
            test('identity_forward_geometry_hdr_amount_disable_project_roundtrip',geometry)
            def freeze():
                locked,_=probe(base,dabs=[dict(freeze=1),warp]);assert np.array_equal(locked,base)
                freed,_=probe(base,dabs=[dict(freeze=1),dict(freeze=0),warp]);assert not np.array_equal(freed,base)
                inverted,_=probe(base,dabs=[dict(freeze=1),dict(freeze=2),warp]);assert np.array_equal(inverted,freed)
                frozen=dict(tool=7,size=45,density=1,pressure=1,**{'from':[80,64],'to':[80,64]})
                out,_=probe(base,dabs=[frozen,warp]);assert np.array_equal(out[60:68,75:85],base[60:68,75:85])
                thaw=dict(frozen,tool=8)
                out,_=probe(base,dabs=[frozen,thaw,warp]);assert np.allclose(out[60:68,75:85],freed[60:68,75:85],atol=1e-6)
                changed,_=probe(base,dabs=[warp]);protected,_=probe(base,dabs=[warp,dict(freeze=1),dict(warp,**{'from':[80,64],'to':[95,64]})]);assert np.array_equal(changed,protected)
            test('freeze_thaw_invert_and_preserve_existing_deformation',freeze)
            def reconstruct():
                restored,_=probe(base,dabs=[warp,dict(reconstruct=1)]);assert np.array_equal(restored,base)
                half,_=probe(base,dabs=[warp,dict(reconstruct=.5)]);assert abs(half[64,80,0]-72.5/160)<1e-6
                brush=dict(tool=1,size=150,density=1,pressure=1,**{'from':[80,64],'to':[80,64]})
                restored,_=probe(base,dabs=[warp,brush]);assert np.array_equal(restored,base)
                outputs=[]
                for tool in [2,3,4,5,6]:
                    moved,_=probe(base,dabs=[warp,dict(warp,tool=tool)]);assert np.isfinite(moved).all();outputs.append(moved)
                assert all(not np.array_equal(outputs[i],outputs[j]) for i in range(5) for j in range(i))
            test('reconstruction_and_distinct_smooth_twirl_pucker_bloat_push',reconstruct)
            def alpha():
                image=base.copy();image[:,:,3]=np.clip((x-45)/30,0,1);image[:,:,:3]*=image[:,:,3,None]
                out,_=probe(image,dabs=[warp]);assert np.allclose(out[:,:,2],out[:,:,3]*2)
                assert np.all(out[out[:,:,3]==0,:3]==0)
                half,_=probe(image,dabs=[warp],amount=.5);assert np.allclose(half,(out+image)/2)
                assert 'error' in raw('brush_probe',**request(base,dabs=[dict(warp,size=-1)]))
                assert 'error' in raw('brush_probe',**request(base,dabs=[warp],cancelled=True))
            test('premultiplied_alpha_hdr_no_halo_invalid_and_cancel',alpha)
            call('window')
            yy,xx=np.mgrid[:320,:420];rgb=np.empty((320,420,3),np.uint8);rgb[:]=[70,85,100];rgb[(xx%30<3)|(yy%30<3)]=[220,150,70]
            path=Path(folder)/'grid.png';Image.fromarray(rgb).save(path)
            def scene():call('window_scene',width=420,height=320,layers=[dict(name='grid',path=str(path))])
            actions=[dict(liquifySize=130,liquifyPressure=100),dict(**{'from':[.4,.5],'to':[.6,.5]}),dict(liquifyMesh=True)]
            def ui():
                scene();before=call('window_brush')
                r=call('window_brush',liquifyDialog=True,actions=actions,accept=False,capture=str(OUT/'workspace.png'));assert r['content']['ready'] and r['hash']==before['hash'] and r['history']==before['history'],r['content']
                r=call('window_brush',liquifyDialog=True,actions=actions,accept=True,pixels=str(OUT/'warped.png'));assert r['content']['ready'] and r['hash']!=before['hash'] and r['history']==before['history']+1,r['content']
                call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==before['hash']
                call('window_key',key='Ctrl+Shift+Z');assert call('window_brush')['hash']==r['hash']
                reopened=call('window_brush',liquifyDialog=True,actions=[],accept=True);assert reopened['hash']==r['hash']
                call('window_design',file=str(OUT/'liquify-ui.huelith'));call('window_open',path=str(OUT/'liquify-ui.huelith'));assert call('window_brush')['hash']==r['hash']
                restored=call('window_brush',liquifyDialog=True,actions=[dict(button='liquifyRestore')],accept=True);assert restored['hash']==before['hash']
            test('actual_preview_cancel_commit_undo_redo_reopen_project_restore',ui)
            def ui_protection():
                scene();before=call('window_brush')
                r=call('window_brush',liquifyDialog=True,actions=[dict(button='liquifyFreezeAll')]+actions,accept=True,capture=str(OUT/'frozen.png'));assert r['hash']==before['hash'] and r['content']['ready'],r['content']
                r=call('window_brush',liquifyDialog=True,actions=[dict(button='liquifyThawAll')]+actions,accept=True);assert r['hash']!=before['hash']
                scene();before=call('window_brush')
                r=call('window_brush',liquifyDialog=True,actions=actions+[dict(key=90,modifiers=67108864)],accept=True);assert r['hash']==before['hash'],r['content']
            test('actual_freeze_reopen_thaw_and_local_undo',ui_protection)
            def selected():
                scene();call('window_control',name='toolButton_2',value=True);call('window_gesture',x=140,y=100,endX=230,endY=220)
                before=call('window_brush',pixels=str(OUT/'selected-before.png'))
                r=call('window_brush',liquifyDialog=True,actions=actions,accept=True,pixels=str(OUT/'selected-after.png'))
                a=np.asarray(Image.open(OUT/'selected-before.png'));b=np.asarray(Image.open(OUT/'selected-after.png'))
                outside=np.ones(a.shape[:2],bool);outside[99:221,139:231]=False
                assert np.array_equal(a[outside],b[outside]) and r['hash']!=before['hash']
                call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==before['hash']
            test('actual_selection_protection_and_single_history_transaction',selected)
            def resources():
                scene()
                mesh_path=str(Path(folder)/'saved.hlmesh')
                r=call('window_brush',liquifyDialog=True,actions=actions+[
                    dict(button='liquifySaveMesh',file=mesh_path)],accept=True)
                assert Path(mesh_path).is_file()
                expected=r['hash']
                scene()
                loaded=call('window_brush',liquifyDialog=True,actions=[
                    dict(button='liquifyLoadMesh',file=mesh_path),
                    dict(liquifyBackdrop=True,liquifyBackdropMode=1,liquifyBackdropOpacity=40)],
                    accept=True,capture=str(OUT/'backdrop-and-mesh.png'))
                assert loaded['hash']==expected
                scene();before=call('window_brush')['hash']
                restored=call('window_brush',liquifyDialog=True,actions=[
                    dict(button='liquifyLoadMesh',file=mesh_path),dict(key=90,modifiers=67108864)],accept=True)
                assert restored['hash']==before
                bad=Path(folder)/'invalid.hlmesh';bad.write_text('{invalid')
                unchanged=call('window_brush',liquifyDialog=True,actions=[
                    dict(button='liquifyLoadMesh',file=str(bad))],accept=True)
                assert unchanged['hash']==before
            test('mesh_save_load_local_undo_invalid_recovery_background_no_pixel_change',resources)
            def tablet():
                hashes=[]
                for pressure,enabled in [(0,True),(.25,True),(1,True),(.25,False)]:
                    scene();before=call('window_brush')['hash']
                    r=call('window_brush',liquifyDialog=True,actions=[
                        dict(liquifySize=130,liquifyPressure=100,liquifyStylusPressure=enabled),
                        dict(pressure=pressure,**{'from':[.4,.5],'to':[.6,.5]})],accept=True)
                    hashes.append(r['hash'])
                assert hashes[0]==before and hashes[1]!=hashes[2] and hashes[2]==hashes[3]
            test('actual_tablet_zero_quarter_full_and_pressure_preference',tablet)
            def large():
                yy,xx=np.mgrid[:800,:1200];image=np.ones((800,1200,4),np.float32);image[:,:,0]=xx/1200;image[:,:,1]=yy/800;image[:,:,2]=2
                out,r=probe(image,dabs=[dict(tool=0,size=360,density=.8,pressure=1,**{'from':[510,400],'to':[600,400]})])
                assert abs(out[400,600,0]-.425)<1e-5 and np.array_equal(out[:200],image[:200])
                assert r['meshWidth']<=513 and r['meshHeight']<=513 and r['elapsedMs']<1000
                print(dict(full_960000_pixel_ms=r['elapsedMs']),flush=True)
            test('normalized_mesh_full_resolution_geometry_and_bounded_cost',large)

        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
