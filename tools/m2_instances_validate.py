"""Annotated natural images + actual production Qt single-person selection workflows."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'research/m2/instances'
OUT=ROOT/'docs/test-evidence/M2/instances'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert not isinstance(r.get('error'),str),r;return r
    def wait():
        deadline=time.monotonic()+45
        longest=0
        while time.monotonic()<deadline:
            start=time.perf_counter();call('pump',waitMs=20);longest=max(longest,time.perf_counter()-start)
            r=call('semantic_window_state')
            if r['error']:raise AssertionError(r)
            if r['ready']:return r,longest
        raise TimeoutError(r)
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:
            records.append(dict(name=name,passed=False,error=traceback.format_exc()))
            raw('semantic_window_state',reject=True)
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-instances-') as temporary:
        call('init',settings=temporary)
        try:
            def quality():
                measured=[]
                for name in ['FudanPed00001','FudanPed00002','FudanPed00003','FudanPed00004','FudanPed00005','PennPed00001','PennPed00002']:
                    folder=OUT/name;folder.mkdir(exist_ok=True)
                    r=call('instance_probe',path=str(ASSETS/(name+'.png')),output=str(folder))
                    people=[i for i in r['instances'] if i['category']==1]
                    assert people,r
                    w,h=people[0]['width'],people[0]['height']
                    truth=np.array(Image.open(ASSETS/(name+'_mask.png')).resize((w,h),Image.Resampling.NEAREST))
                    masks=[np.array(Image.open(i['mask'])) for i in people]
                    matching=[];ious=[]
                    for label in np.unique(truth):
                        if label==0:continue
                        gt=truth==label
                        values=[np.count_nonzero((m>127)&gt)/np.count_nonzero((m>127)|gt) for m in masks]
                        matching.append(int(np.argmax(values)));ious.append(float(max(values)))
                    assert len(set(matching))==len(matching),'two annotated people share a candidate'
                    assert min(ious)>.5 and np.mean(ious)>.8,(name,ious)
                    assert r['ms']<15000,r['ms']
                    assert all(any((m>0).ravel()) and np.any((m>0)&(m<255)) for m in masks)
                    measured.append(dict(file=name,milliseconds=r['ms'],people=len(people),annotated=len(ious),iou=ious,
                                         categories=[i['category'] for i in r['instances']]))
                (OUT/'quality.json').write_text(json.dumps(measured,indent=2),encoding='utf-8')
            test('seven_annotated_natural_images_distinct_people_soft_masks_objects',quality)
            def failure():
                blank=Path(temporary)/'blank.png';Image.new('RGB',(640,400),(128,128,128)).save(blank)
                assert call('instance_probe',path=str(blank))['instances']==[]
                for kw in [dict(cancelled=True),dict(model=str(Path(temporary)/'missing.onnx')),dict(cancelAfterMs=350)]:
                    start=time.perf_counter();r=raw('instance_probe',path=str(ASSETS/'FudanPed00001.png'),**kw)
                    assert 'error' in r,r
                    if 'cancelAfterMs' in kw:assert time.perf_counter()-start<3
                corrupt=Path(temporary)/'corrupt.onnx';corrupt.write_bytes(b'invalid')
                assert 'error' in raw('instance_probe',path=str(blank),model=str(corrupt))
                call('semantic_window',path=str(blank),target=3);s,_=wait();assert s['count']==0 and s['selected']==0
                assert call('semantic_window_state',accept=True)['result']==0
                call('semantic_window_state',reject=True)
            test('empty_image_missing_corrupt_cancel_and_no_false_accept',failure)
            def interaction():
                call('semantic_window',path=str(ASSETS/'FudanPed00001.png'),target=3)
                s,pump=wait();assert s['count']==2,s;assert pump<.6,pump
                masks=[]
                for i in range(2):
                    p=OUT/f'person-{i}.png';s=call('semantic_window_state',person=i,save=str(p))
                    assert s['selected']>100;masks.append(np.array(Image.open(p)))
                assert np.count_nonzero((masks[0]>127)&(masks[1]>127))<50
                for i,mask in enumerate(masks):
                    ys,xs=np.where(mask>250);k=len(xs)//2
                    s=call('semantic_window_state',click=[float((xs[k]+.5)/mask.shape[1]),float((ys[k]+.5)/mask.shape[0])])
                    assert s['person']==i,s
                call('semantic_window_state',capture=str(OUT/'person-picker.png'))
                call('semantic_window_state',target=4);wait()
                call('semantic_window_state',save=str(OUT/'subject.png'))
                a=np.array(Image.open(OUT/'subject.png'))
                call('semantic_window_state',target=5,save=str(OUT/'background.png'))
                b=np.array(Image.open(OUT/'background.png'))
                assert np.array_equal(a.astype(int)+b,255*np.ones(a.shape))
                call('semantic_window_state',target=6);s,_=wait();assert s['count']==2
                call('semantic_window_state',target=0);wait()
                start=time.perf_counter();call('semantic_window_state',target=3);wait();assert time.perf_counter()-start<.25
                call('semantic_window_state',accept=True);assert call('semantic_window_state')['result']==1
            test('actual_qt_two_thumbnails_canvas_click_subject_background_lazy_cache',interaction)
            def menus():
                call('window');call('window_open',path=str(ASSETS/'FudanPed00001.png'))
                before=call('window_probe')['history']
                call('window_action',text='人物…',dialog=dict(accept=False));assert call('window_probe')['history']==before
                for action in ['主体','背景','对象…','人物…']:
                    call('window_action',text=action,**({'dialog':dict(accept=True)} if action.endswith('…') else {}))
                    assert call('window_state')['selected'],action
                    assert call('window_probe')['history']==before+1
                    call('window_key',key='Ctrl+Z');assert call('window_probe')['history']==before
            test('real_main_menu_subject_background_object_person_cancel_single_undo',menus)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,indent=2,ensure_ascii=False),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))

if __name__=='__main__':main()
