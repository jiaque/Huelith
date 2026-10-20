"""Camera JPEG brightness reference, missing/PQ paths and RAW UI undo."""
import ctypes,json,os,tempfile,time,subprocess
from PIL import Image
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2-E/embedded'
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))];os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command
    fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert 'error' not in r,r;return r
    def wait():
        deadline=time.monotonic()+90
        while time.monotonic()<deadline:
            call('pump',waitMs=30);r=call('raw_window_state')
            if r.get('idle') and r.get('previewPresented',0)>0 and r['previewRequested']==r['previewPresented']:return r
        raise TimeoutError(r)
    with tempfile.TemporaryDirectory(prefix='huelith-embedded-') as folder:
        call('init',settings=folder)
        try:
            source=str(ROOT/'research/m0/samples/r10-5871.CR3')
            r=call('embedded_probe',source=source,output=str(OUT/'camera-preview.png'))
            assert r['bits']==8 and r['width']>500 and r['sameExposure']==0 and abs(r['darkExposure']-1)<.08,r
            records=[dict(case='real-camera-embedded-jpeg-and-one-stop-reference',**r)]
            missing=Path(folder)/'missing.raw';missing.write_bytes(b'unsupported raw without jpeg')
            assert 'error' in raw('embedded_probe',source=str(missing))
            pq=raw('embedded_probe',source=source,metadata={'Camera:HDRPQ':1})
            assert 'HDR-PQ' in pq['error'],pq
            records.append(dict(case='missing-preview-and-pq-policy',passed=True,note='PQ metadata path; not a real PQ sample'))
            call('window');call('raw_window',path=source,recipe={'denoise':False});before=wait()['recipe']
            call('raw_window_state',click='rawCameraBrightness');r=wait();exposure=r['recipe']['tone']['exposure']
            assert -3<=exposure<=3 and abs(exposure)>.01,r
            call('raw_window_state',capture=str(OUT/'brightness-dialog.png'))
            records.append(dict(case='actual-raw-camera-brightness-control',exposure=exposure))
            call('raw_window_state',reject=True)
            fallback=Path(folder)/'unsupported.raw';thumb=Path(folder)/'thumb.jpg';main=Path(folder)/'main.jpg'
            Image.open(OUT/'camera-preview.png').convert('RGB').resize((320,213)).save(thumb);Image.new('RGB',(10,10)).save(main)
            exe=ROOT/'build/windows-release/tools/exiftool/exiftool.exe'
            subprocess.run([str(exe),'-config','','-overwrite_original','-ThumbnailImage<='+str(thumb),str(main)],check=True,capture_output=True)
            fallback.write_bytes(main.read_bytes())
            r=call('embedded_probe',source=str(fallback));assert r['width']==320 and r['bits']==8,r
            call('raw_window',path=str(fallback),recipe={'denoise':False})
            for _ in range(300):
                call('pump',waitMs=30);r=call('raw_window_state')
                if r.get('idle'):break
            r=call('dng_ui',embedded=True);assert r['offered'] and r['accepted'] and r['rawBytes']==0 and r['width']==320,r
            records.append(dict(case='unsupported-decode-explicit-embedded-jpeg-fallback',**r))
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,indent=2),encoding='utf-8');print(json.dumps(records,ensure_ascii=True))
if __name__=='__main__':main()
