"""RAW finish sidecars, source integrity, cancellation and image-save snapshot queue."""
import ctypes,hashlib,io,json,os,shutil,tempfile,time,traceback
from pathlib import Path
from PIL import Image,ImageCms
import numpy as np
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/raw-output'
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))];os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert 'error' not in r,r;return r
    def state(**kw):return call('raw_window_state',**kw)
    def wait(done=False):
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            call('pump',waitMs=30);r=state()
            if done and r['result']==2:return r
            if not done and r['idle'] and r['previewPresented']>0 and r['previewPresented']==r['previewRequested']:return r
        raise TimeoutError(r)
    def output(**kw):return call('export_queue',window=True,windowName='rawExportDialog',**kw)
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-raw-output-') as directory:
        folder=Path(directory);call('init',settings=str(folder));call('window')
        try:
            def integrity():
                path=folder/'原始.raw';path.write_bytes(b'original-camera-bytes');sidecar=Path(str(path)+'.hraw.json')
                source=dict(path=str(path),recipe=dict(tone=dict(version=8,exposure=.7),rating=4,rejected=True))
                result=call('raw_sidecar_probe',sources=[source],save=True);assert result['recipes'][0]==source['recipe'];before=sidecar.read_bytes()
                assert path.read_bytes()==b'original-camera-bytes'
                invalid=dict(path=str(folder/'second.raw'),recipe=dict(tone=dict(version=8,exposure=999)))
                Path(invalid['path']).write_bytes(b'second-source')
                for options in [dict(sources=[dict(source,recipe={}),invalid]),dict(sources=[source],cancelled=True),dict(sources=[source,source]),dict(sources=[dict(source,bytes='different-source')])]:
                    assert 'error' in raw('raw_sidecar_probe',save=True,**options)
                    assert sidecar.read_bytes()==before
                    assert not Path(invalid['path']+'.hraw.json').exists()
                path.write_bytes(b'replacement');assert 'error' in raw('raw_sidecar_probe',sources=[dict(path=str(path))]);path.write_bytes(b'original-camera-bytes')
                sidecar.write_text('{}',encoding='utf-8');assert 'error' in raw('raw_sidecar_probe',sources=[source]);sidecar.write_bytes(before)
            test('sidecar_roundtrip_source_hash_unicode_atomic_preflight_cancel_corruption',integrity)
            path=folder/'photo.CR3';shutil.copyfile(ROOT/'research/m0/samples/r10-5871.CR3',path);digest=hashlib.sha256(path.read_bytes()).hexdigest();sidecar=Path(str(path)+'.hraw.json')
            def done():
                call('raw_window',path=str(path),recipe={},linked=True);wait();state(exposure=.75);state(combo='rawProfileChoice',text='Huelith 风景');wait();state(click='rawRating4')
                start=time.perf_counter();state(click='rawDone');elapsed=(time.perf_counter()-start)*1000;wait(done=True)
                assert elapsed<150,elapsed
                saved=json.loads(sidecar.read_text(encoding='utf-8'));assert saved['recipe']['tone']['exposure']==.75 and saved['recipe']['rating']==4 and saved['recipe']['profile']['id']=='huelith.landscape'
                assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
                before=sidecar.read_bytes();call('raw_window',path=str(path),recipe={},linked=True);wait();r=state()['recipes'][0];assert r['tone']['exposure']==.75 and r['rating']==4
                state(exposure=-2);wait();state(reject=True);assert sidecar.read_bytes()==before
                (OUT/'done-performance.json').write_text(json.dumps(dict(input_ms=elapsed)),encoding='utf-8')
            test('actual_done_background_sidecar_reopen_rating_profile_cancel_keeps_saved_recipe',done)
            def image_save():
                call('raw_window',path=str(path),recipe={},linked=True);wait();state(preferences=dict(accept=True,rawMemoryPreference=128));r=wait();assert r['cacheMemoryBytes']==0 and r['cacheDiskBytes']>0
                start=time.perf_counter();state(click='rawSaveImages');elapsed=(time.perf_counter()-start)*1000;assert elapsed<150,elapsed
                deadline=time.monotonic()+120
                while time.monotonic()<deadline:
                    call('pump',waitMs=30);r=raw('export_queue',window=True,windowName='rawExportDialog')
                    if 'error' not in r:break
                assert 'error' not in r,r
                output(exportQueueDirectory=str(folder),exportQueueSizes='320',exportQueueNaming='saved-{index}',exportQueueFormat=1,exportQueueProfile=1,click='exportQueueStart')
                state(reject=True)
                deadline=time.monotonic()+120
                while time.monotonic()<deadline:
                    call('pump',waitMs=30);items=output()['items']
                    if items and all(i['status']>=3 for i in items):break
                assert len(items)==1 and items[0]['status']==3,items
                file=Path(items[0]['path']);data=file.read_bytes();assert data[24]==16
                with Image.open(file) as image:
                    assert image.size==(320,213),image.size
                    profile=ImageCms.getProfileName(ImageCms.ImageCmsProfile(io.BytesIO(image.info['icc_profile'])));assert 'Adobe' in profile,profile
                    assert np.array(image).std()>10
                shutil.copyfile(file,OUT/'actual-save-adobergb16.png')
                assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
                assert json.loads(sidecar.read_text(encoding='utf-8'))['recipe']['tone']['exposure']==.75
                output(capture=str(OUT/'save-images.png'))
                (OUT/'export-details.json').write_text(json.dumps(dict(input_ms=elapsed,items=items,profile=profile),ensure_ascii=False,indent=2),encoding='utf-8')
            test('actual_save_images_source_snapshot_png16_icc_size_queue_survives_raw_close',image_save)
            def open_variants():
                def accepted():
                    deadline=time.monotonic()+120
                    while time.monotonic()<deadline:
                        call('pump',waitMs=30);r=state()
                        if r['result']==1:return r
                    raise TimeoutError(r)
                call('raw_window',path=str(path),recipe={},linked=True);wait();state(exposure=1.25);wait();state(outputAction='rawOpenSource');r=accepted()
                assert r['recipes'][0]['tone']['exposure']==1.25
                assert json.loads(sidecar.read_text(encoding='utf-8'))['recipe']['tone']['exposure']==1.25
                before=sidecar.read_bytes();call('raw_window',path=str(path),recipe={},linked=True);wait();state(exposure=-1);wait();state(outputAction='rawOpenCopy');r=accepted()
                assert r['recipes'][0]['tone']['exposure']==-1 and sidecar.read_bytes()==before
                call('raw_window',path=str(path),recipe={},linked=True);wait();state(exposure=.3);wait();state(outputAction='rawOpenPixels');r=accepted()
                assert r['recipes']==[{}] and json.loads(sidecar.read_text(encoding='utf-8'))['recipe']['tone']['exposure']==.3
                assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
            test('open_source_saves_settings_open_copy_preserves_settings_raster_open_explicit',open_variants)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8');raise SystemExit(not all(x['passed'] for x in records))
if __name__=='__main__':main()
