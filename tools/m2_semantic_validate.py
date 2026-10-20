"""Production C++ inference, annotated-image quality and actual Qt menu interactions."""
import base64, ctypes, io, json, os, tempfile, time, traceback
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'docs/test-evidence/M2/semantic'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    dll = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = dll.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p
    def raw(op, **kw): return json.loads(fn(json.dumps(dict(op=op, **kw)).encode()))
    def call(op, **kw):
        r = raw(op, **kw)
        assert 'error' not in r, r
        return r
    records=[]
    def test(name, body):
        try: body(); records.append(dict(name=name, passed=True))
        except Exception: records.append(dict(name=name, passed=False, error=traceback.format_exc()))
        print(records[-1], flush=True)
    samples=ROOT/'research/m2/semantic/samples'
    landscape=samples/'ADE_val_00000001.jpg'
    with tempfile.TemporaryDirectory(prefix='huelith-semantic-') as folder:
        call('init', settings=folder)
        try:
            def quality():
                measured=[]
                for number in (1,2):
                    stem=f'ADE_val_{number:08d}'
                    start=time.perf_counter()
                    r=call('semantic_probe',path=str(samples/(stem+'.jpg')),labels=str(OUT/(stem+'-labels.png')))
                    elapsed=time.perf_counter()-start
                    predicted=np.array(Image.open(OUT/(stem+'-labels.png')))
                    truth=np.array(Image.open(samples/(stem+'.png')).resize((r['width'],r['height']),Image.Resampling.NEAREST)).astype(int)-1
                    scores={}
                    for label,name in ((2,'sky'),(4,'tree'),(9,'grass'),(17,'plant')):
                        a=predicted==label;b=truth==label
                        union=np.count_nonzero(a|b)
                        if union: scores[name]=float(np.count_nonzero(a&b)/union)
                    measured.append(dict(sample=stem,seconds=elapsed,iou=scores,counts=r['counts']))
                    assert scores['sky']>.9, scores
                    if number==1: assert scores['grass']>.8,scores
                    assert sum(r['counts'])==r['width']*r['height']
                (OUT/'quality.json').write_text(json.dumps(measured,indent=2),encoding='utf-8')
            test('actual_cpp_annotated_landscape_quality_and_all_class_counts',quality)
            def failures():
                assert 'error' in raw('semantic_probe',path=str(landscape),cancelled=True)
                assert 'error' in raw('semantic_probe',path=str(landscape),model=str(Path(folder)/'missing.onnx'))
                invalid=Path(folder)/'invalid.onnx';invalid.write_bytes(b'not an ONNX graph')
                assert 'error' in raw('semantic_probe',path=str(landscape),model=str(invalid))
                assert 'error' in raw('semantic_probe',path=str(Path(folder)/'missing.png'))
            test('cancel_missing_model_corrupt_model_missing_image_fail_explicitly',failures)
            call('window')
            def menu():
                call('window_open',path=str(landscape))
                before=call('window_probe')['history']
                for name in ('天空','草地','植被'):
                    call('window_action',text=name)
                    assert call('window_state')['selected']
                    assert call('window_probe')['history']==before+1
                    count=call('window_interaction_state')['rangeCount']
                    assert count>0
                    call('window_key',key='Ctrl+Z')
                    assert call('window_probe')['history']==before
                    assert not call('window_state')['selected']
                call('window_action',text='天空')
                call('window_capture',path=str(OUT/'sky-selected.png'))
            test('actual_selection_menu_three_targets_single_undo_and_visible_selection',menu)
            def person():
                call('window_open',path=str(ROOT/'research/m0/portrait-astronaut.png'))
                before=call('window_probe')['history']
                call('window_action',text='人物…',dialog=dict(accept=False,capture=str(OUT/'person-dialog.png')))
                assert call('window_probe')['history']==before
                call('window_action',text='人物…',dialog=dict(accept=True))
                assert call('window_state')['selected']
                assert call('window_probe')['history']==before+1
                call('window_key',key='Ctrl+Z')
                assert not call('window_state')['selected']
            test('person_preview_cancel_commit_and_undo',person)
            def bitmap():
                mask=np.zeros((80,100),np.uint8);mask[:,:50]=255
                stream=io.BytesIO();Image.fromarray(mask).save(stream,format='PNG')
                component=dict(mode='bitmap',png=base64.b64encode(stream.getvalue()).decode())
                local=dict(components=[component],tone=dict(version=5,exposure=1))
                points=[[25,40],[75,40]]
                r=call('raw_local_probe',recipe=dict(locals=[local]),points=points)['samples']
                assert r[0]['mask']==255 and r[1]['mask']==0,r
                assert abs(r[0]['rgba'][0]-.4)<1e-5 and abs(r[1]['rgba'][0]-.2)<1e-5,r
                assert all(p['rgba'][3]==.5 for p in r)
                local['components'].append(dict(component,subtract=True))
                r=call('raw_local_probe',recipe=dict(locals=[local]),points=points)['samples']
                assert all(p['mask']==0 for p in r)
                local['invert']=True
                r=call('raw_local_probe',recipe=dict(locals=[local]),points=points)['samples']
                assert all(p['mask']==255 for p in r)
                for bad in ('%%%','',base64.b64encode(b'not png').decode()):
                    assert 'error' in raw('raw_local_probe',recipe=dict(locals=[dict(components=[dict(mode='bitmap',png=bad)])]),points=points)
            test('bitmap_local_exposure_alpha_subtract_invert_and_malformed_png',bitmap)
            def raw_masks():
                call('raw_window',path=str(ROOT/'research/m0/samples/r10-8185.CR3'),denoise=False)
                def wait():
                    deadline=time.monotonic()+120
                    while time.monotonic()<deadline:
                        call('pump',waitMs=40);s=call('raw_window_state')
                        if s['previewPresented']>0 and s['previewPresented']==s['previewRequested']:return s
                    raise TimeoutError(s)
                wait()
                call('raw_window_state',click='rawSemantic3',semantic=dict(immediateCancel=True))
                s=call('raw_window_state');assert not s['recipe'].get('locals')
                # Use the non-auto-closing person dialog to choose a landscape category.
                call('raw_window_state',click='rawSemantic3',semantic=dict(target=0,accept=True,capture=str(OUT/'raw-sky-dialog.png')))
                assert not call('raw_window_state')['recipe'].get('locals')  # This scene has no sky.
                call('raw_window_state',click='rawSemantic3',semantic=dict(target=2,accept=True,capture=str(OUT/'raw-vegetation-dialog.png')))
                s=wait()
                assert len(s['recipe'].get('locals',[]))==1,s['recipe']
                original=s['recipe']['locals'][0]
                assert original['name']=='植被'
                assert original['components'][0]['mode']=='bitmap'
                call('raw_window_state',control='rawExposure',value=.5);wait();call('pump',waitMs=400)
                s=call('raw_window_state',click='rawUndo');assert s['recipe']['locals'][0]['tone']['exposure']==0
                call('raw_window_state',click='rawUndo');assert not call('raw_window_state')['recipe'].get('locals')
                call('raw_window_state',click='rawRedo');wait()
                s=call('raw_window_state',accept=True)
                assert s['result']==1,s
                call('raw_window_state',save=str(Path(folder)/'semantic.huelith'))
                (OUT/'raw-recipe.json').write_text(json.dumps(s['recipe'],ensure_ascii=False,indent=2),encoding='utf-8')
            test('real_raw_semantic_cancel_local_exposure_undo_redo_commit_project_roundtrip',raw_masks)
        finally: call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))

if __name__=='__main__': main()
