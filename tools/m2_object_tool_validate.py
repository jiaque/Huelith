"""Actual object-tool pointer gestures, selection algebra, cache and cancellation."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/object-tool';ASSETS=ROOT/'research/m2/instances'
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    def state(**kw):return call('object_window',**kw)
    def wait():
        deadline=time.monotonic()+45;maximum=0
        while time.monotonic()<deadline:
            start=time.perf_counter();call('pump',waitMs=20);maximum=max(maximum,time.perf_counter()-start)
            r=state();assert not r['failed'],r
            if r['ready']:return r,maximum
        raise TimeoutError(r)
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-object-') as folder:
        call('init',settings=folder);call('window');call('window_open',path=str(ASSETS/'FudanPed00001.png'))
        truth=np.array(Image.open(ASSETS/'FudanPed00001_mask.png'));points=[];bounds=[]
        for label in [1,2]:
            ys,xs=np.where(truth==label);points.append([float(np.median(xs)),float(np.median(ys))]);bounds.append([int(xs.min()),int(ys.min()),int(xs.max()),int(ys.max())])
        def save(name):
            path=OUT/(name+'.png');state(save=str(path));return np.array(Image.open(path))
        try:
            def single():
                state(activate=True);r,pump=wait();assert r['count']>=2 and pump<.4,(r,pump)
                base=call('window_probe')['history'];masks=[]
                for i,p in enumerate(points):
                    r=state(hover=p);assert r['hover']>=0,r
                    state(gesture=[p,p]);m=save('person'+str(i));masks.append(m)
                    gt=truth==i+1;iou=np.count_nonzero((m>127)&gt)/np.count_nonzero((m>127)|gt);assert iou>.65,(i,iou)
                    assert state()['ready'];assert call('window_probe')['history']==base+i+1
                assert np.count_nonzero((masks[0]>127)&(masks[1]>127))<60
                state(capture=str(OUT/'object-hover.png'))
            test('real_toolbar_async_two_people_hover_click_unique_soft_masks',single)
            def algebra():
                state(gesture=[points[0],points[0]]);first=save('first')
                state(gesture=[points[1],points[1]],modifiers=33554432);both=save('added');assert np.count_nonzero(both>127)>np.count_nonzero(first>127)*1.6
                state(gesture=[points[1],points[1]],modifiers=134217728);removed=save('subtracted');assert np.count_nonzero(removed>127)<=np.count_nonzero(first>127)+30
                call('window_key',key='Ctrl+Z');assert np.array_equal(save('undo'),both);wait()
                call('window_key',key='Ctrl+Shift+Z');assert np.array_equal(save('redo'),removed);wait()
            test('shift_add_alt_subtract_single_undo_redo_inference_invalidation',algebra)
            def region():
                x0,y0,x1,y1=bounds[0];p=[[x0-3,y0-3],[x1+3,y1+3]]
                state(mode=0,gesture=p);rectangle=save('rectangle');assert np.count_nonzero(rectangle>127)>100
                h=call('window_probe')['history'];state(gesture=[[10,10],[200,200],[220,220]],escape=True);assert call('window_probe')['history']==h
                polygon=[[x0-3,y0-3],[x1+3,y0-3],[x1+3,y1+3],[x0-3,y1+3],[x0-3,y0-3]]
                state(mode=1,gesture=polygon);assert np.array_equal(save('lasso'),rectangle)
                state(showAll=True,capture=str(OUT/'all-objects.png'))
            test('rectangle_closed_lasso_escape_and_all_object_overlay',region)
            def options():
                state(all=False);assert not state()['ready'];wait()
                call('window_key',key='Shift+W');assert state()['tool']!=28
                state(activate=True);wait();assert state()['tool']==28
                blank=Path(folder)/'blank.png';Image.new('RGB',(500,400),(128,128,128)).save(blank)
                call('window_open',path=str(blank));state(activate=True);r,_=wait();assert r['count']==0
                h=call('window_probe')['history'];state(gesture=[[200,200],[200,200]]);assert call('window_probe')['history']==h
            test('sample_layers_toggle_W_group_blank_no_false_selection',options)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))
if __name__=='__main__':main()
