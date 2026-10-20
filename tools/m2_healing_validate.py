"""Independent pixel references and actual RAW repair controls, using production DLL."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/healing'
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
    def stroke(**kw):return dict(version=1,points=[[.5,.5]],radius=.09,feather=.5,opacity=1,offsetX=-.25,offsetY=0,clone=True,**kw)
    def probe(a,s,**kw):
        r=call('healing_probe',width=a.shape[1],height=a.shape[0],pixels=a.flatten().tolist(),strokes=s,**kw)
        return np.array(r['pixels']).reshape(a.shape),r
    records=[]
    def test(name,f):
        try:f();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-heal-') as folder:
        call('init',settings=folder)
        def cloning():
            w=h=96;y,x=np.mgrid[:h,:w]
            a=np.stack([x/w,y/h,np.sin(x/4)*.1+.2,np.full((h,w),.5)],axis=-1).astype(np.float32)
            a[:,:,:3]*=a[:,:,3,None]
            s=stroke();b,_=probe(a,[s]);distance=np.hypot(x-48,y-48);r=.09*96
            mask=np.clip((r-distance)/(r*.5),0,1);mask=mask**2*(3-2*mask)
            sample=np.roll(a,24,axis=1)
            expected=a+(sample-a)*mask[:,:,None]
            assert np.max(np.abs(b-expected))<2e-7
            assert np.array_equal(b[:,:,3],a[:,:,3])
            s['opacity']=0;z,_=probe(a,[s]);assert np.array_equal(z,a)
            s['opacity']=1;s['enabled']=False;z,_=probe(a,[s]);assert np.array_equal(z,a)
        test('numpy_clone_feather_opacity_alpha_and_disabled',cloning)
        def healing():
            w=h=96;y,x=np.mgrid[:h,:w];base=.1+x*.002
            a=np.stack([base,base,base,np.ones_like(base)],axis=-1).astype(np.float32)
            disk=(x-48)**2+(y-48)**2<16;a[disk,:3]=.9
            s=stroke();s['clone']=False
            b,_=probe(a,[s]);assert np.max(np.abs(b[disk,0]-base[disk]))<.015, float(np.max(np.abs(b[disk,0]-base[disk])))
            outside=(x-48)**2+(y-48)**2>(.09*96)**2
            assert np.array_equal(b[outside],a[outside])
            c,r=probe(a,[s],automatic=True);assert abs(r['strokes'][0]['offsetX'])+abs(r['strokes'][0]['offsetY'])>0
            assert np.mean(c[disk,0])<.3
        test('heal_removes_spot_preserves_sloping_light_and_outside_pixels_auto_source',healing)
        def large():
            w=h=512;y,x=np.mgrid[:h,:w];base=.1+x/w*.4
            a=np.stack([base,base,base,np.ones_like(base)],axis=-1).astype(np.float32)
            disk=(x-256)**2+(y-256)**2<35**2;a[disk,:3]=.9
            s=stroke();s['clone']=False;s['radius']=.15
            b,r=probe(a,[s]);error=float(np.max(np.abs(b[disk,0]-base[disk])))
            assert error<.002,error
            assert r['elapsedMs']<1500,r['elapsedMs']
            print(dict(large_spot_max_error=error,core_ms=r['elapsedMs']),flush=True)
        test('large_spot_multiscale_illumination_and_performance',large)
        def errors():
            a=np.ones((16,16,4),np.float32)
            for update in [dict(radius=-1),dict(points=[]),dict(offsetX=2),dict(opacity=2),dict(unknown=1)]:
                s=stroke();s.update(update)
                assert 'error' in raw('healing_probe',width=16,height=16,pixels=a.flatten().tolist(),strokes=[s])
            assert 'error' in raw('healing_probe',width=16,height=16,pixels=a.flatten().tolist(),strokes=[stroke()],cancelled=True)
        test('invalid_recipe_and_cancellation',errors)
        def gui():
            call('window');call('raw_window',paths=[str(ROOT/'research/m0/samples/r10-5871.CR3')]*2,denoise=False)
            def wait():
                deadline=time.monotonic()+120
                while time.monotonic()<deadline:
                    call('pump',waitMs=30);s=call('raw_window_state')
                    if s['previewPresented'] and s['previewPresented']==s['previewRequested']:return s
                raise TimeoutError(s)
            wait();call('raw_window_state',click='rawRail4')
            call('raw_window_state',control='rawHealingSize',value=100)
            call('raw_window_state',stroke=[[.35,.45],[.36,.46],[.37,.47]])
            s=wait();assert len(s['recipe']['healing'])==1,s
            before=s['recipe']['healing'][0]
            call('raw_window_state',control='rawHealingOpacity',value=40);wait();call('pump',waitMs=450)
            call('raw_window_state',click='rawUndo');s=wait();assert s['recipe']['healing'][0]['opacity']==1
            call('raw_window_state',click='rawRedo');s=wait();assert s['recipe']['healing'][0]['opacity']==.4
            call('raw_window_state',combo='rawHealingType',comboIndex=1);wait();call('pump',waitMs=450)
            s=call('raw_window_state');assert s['recipe']['healing'][0]['clone']
            p=np.array(before['points'][0])+[before['offsetX'],before['offsetY']]
            call('raw_window_state',stroke=[p.tolist(),(p+[.03,.02]).tolist()]);s=wait()
            assert abs(s['recipe']['healing'][0]['offsetX']-before['offsetX'])>.01
            call('raw_window_state',capture=str(OUT/'raw-repair.png'))
            call('raw_window_state',click='rawHealingDelete');s=wait();assert not s['recipe']['healing']
            call('raw_window_state',click='rawUndo');s=wait();assert len(s['recipe']['healing'])==1
            # Camera Raw modifiers: Ctrl creates a spot and drags its source.
            call('raw_window_state',stroke=[[.6,.6],[.5,.6]],modifiers=0x04000000);s=wait()
            assert len(s['recipe']['healing'])==2
            spot=s['recipe']['healing'][-1]
            assert len(spot['points'])==1 and abs(spot['offsetX']+.1)<.004,spot
            call('raw_window_state',stroke=[[.63,.62],[.63,.62]],modifiers=0x02000000);s=wait()
            assert len(s['recipe']['healing'][-1]['points'])==2
            old=s['recipe']['healing'][-1]['offsetX']
            call('raw_window_state',click='rawHealingRefresh');s=wait()
            assert s['recipe']['healing'][-1]['offsetX']!=old
            recipe=s['recipe']
            call('raw_window_state',click='rawHealingVisualize');wait()
            call('raw_window_state',capture=str(OUT/'raw-visualize-spots.png'))
            assert call('raw_window_state')['recipe']==recipe
            call('raw_window_state',click='rawHealingVisualize');wait()
            call('raw_window_state',stroke=[[.58,.58],[.66,.65]],modifiers=0x08000000);s=wait()
            assert len(s['recipe']['healing'])==1
            call('raw_window_state',click='rawUndo');s=wait();assert len(s['recipe']['healing'])==2
            call('raw_window_state',click='rawSynchronize',sync=dict(groups=[13]));s=wait()
            assert s['recipes'][0]['healing']==s['recipes'][1]['healing']
            assert call('raw_window_state',accept=True)['result']==1
            call('raw_window_state',save=str(Path(folder)/'repair.huelith'))
        test('actual_raw_draw_edit_move_source_delete_undo_redo_full_commit_save',gui)
        call('shutdown')
    report=dict(passed=sum(r['passed'] for r in records),total=len(records),tests=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
