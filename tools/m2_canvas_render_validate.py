"""Native Qt edits: latest-only full-resolution rendering, cancellation and UI latency."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/canvas-render'

def main():
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64');handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows';OUT.mkdir(parents=True,exist_ok=True)
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert not r.get('error'),r;return r
    def status(**kw):return call('window_brush',previewState=True,**kw)
    def wait():
        start=time.perf_counter();heartbeats=[]
        while time.perf_counter()-start<30:
            r=status();assert not r['error'],r
            if not r['pending']:return dict(settleMs=(time.perf_counter()-start)*1000,maxEventMs=max(heartbeats,default=0))
            tick=time.perf_counter();call('pump',waitMs=10);heartbeats.append((time.perf_counter()-tick)*1000)
        raise TimeoutError(status())
    results=[]
    def test(name,body):
        try:results.append(dict(name=name,passed=True,detail=body()))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(results[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-canvas-render-') as temporary:
        folder=Path(temporary);call('init',settings=str(folder));call('window')
        y,x=np.indices((4000,6000),dtype=np.uint16)
        a=np.empty((4000,6000,4),np.uint8);a[:,:,0]=x%256;a[:,:,1]=y%256;a[:,:,2]=(x//31+y//17)%256;a[:,:,3]=255
        Image.fromarray(a).save(folder/'source.png');del x,y,a
        def scene():
            call('window_scene',width=6000,height=4000,layers=[dict(name='24MP source',path=str(folder/'source.png')),dict(name='overlay',width=1800,height=1200,color='#6080b0',opacity=.7,x=1900,y=1100)])
            wait()
        closed=False
        try:
            scene()
            def changes():
                events=[];frames=set()
                for i in range(24):
                    tick=time.perf_counter();call('window_control',name='layerExposure',value=(i%7-3)*.2);events.append((time.perf_counter()-tick)*1000)
                    generation=status()['proxyGeneration']
                    if generation:frames.add(generation)
                settled=wait();assert len(frames)>=2,frames;assert max(events)<200,events;assert settled['maxEventMs']<150,settled
                assert status(compare=True)['equal']
                call('window_capture',path=str(OUT/'24mp-final.png'))
                return dict(proxyFrames=len(frames),inputMaxMs=max(events),inputMeanMs=sum(events)/len(events),events=events,**settled)
            test('24mp_24_edits_latest_generation_matches_synchronous_full_reference',changes)
            def transform():
                events=[];call('window_chrome',click=0);call('window_key',key='Ctrl+T')
                call('window_mouse',phase='press',handle=7)
                for i in range(1,13):
                    tick=time.perf_counter();call('window_mouse',phase='move',handle=7,dx=-i*2,dy=i);events.append((time.perf_counter()-tick)*1000)
                call('window_mouse',phase='release',handle=7,dx=-24,dy=12);call('window_key',key='Return');settled=wait()
                assert max(events)<200,events;assert status(compare=True)['equal']
                call('window_key',key='Ctrl+Z');wait();assert status(compare=True)['equal']
                call('window_key',key='Ctrl+Shift+Z');wait();assert status(compare=True)['equal']
                return dict(inputMaxMs=max(events),**settled)
            test('24mp_transform_live_preview_commit_undo_redo',transform)
            def replacement():
                call('window_control',name='layerExposure',value=2)
                call('window_scene',width=320,height=200,layers=[dict(name='replacement',color='#112233')]);wait()
                call('pump',waitMs=350);r=status(compare=True);assert r['equal'] and (r['width'],r['height'])==(320,200),r
                return r
            test('document_replacement_discards_old_24mp_future',replacement)
            def close():
                nonlocal closed
                scene();call('window_control',name='layerExposure',value=1.7)
                tick=time.perf_counter();call('shutdown');closed=True;ms=(time.perf_counter()-tick)*1000;assert ms<1500,ms
                return dict(closeMs=ms)
            test('closing_during_render_cancels_and_joins_owned_worker',close)
        finally:
            if not closed:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),'utf-8');return int(not all(r['passed'] for r in results))
if __name__=='__main__':raise SystemExit(main())

