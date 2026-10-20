"""Sensor-preserving DNG round trip through production C++ and independent TIFF parsing."""
import ctypes, hashlib, json, os, struct, tempfile, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2-E/dng'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    source=ROOT/'research/m0/samples/r10-5871.CR3'
    with tempfile.TemporaryDirectory(prefix='huelith-dng-') as folder:
        call('init',settings=folder)
        try:
            recipe={'tone':{'version':8,'exposure':1.25},'denoise':False,'denoiseAmount':30,'demosaic':'malvar-2004'}
            result=call('dng_probe',source=str(source),output=str(OUT/'sensor-copy.dng'),recipe=recipe,compareDevelop=True)
            print(json.dumps(result,ensure_ascii=True),flush=True)
            assert 'error' not in result,result
            assert result['before']['sensorSha256']==result['after']['sensorSha256'],result
            assert result['originalUnchanged'] and result['recipe']==recipe and result['sameSize'],result
            assert result['meanError']<1e-6 and result['maxError']<1e-5,result
            data=(OUT/'sensor-copy.dng').read_bytes();assert data[:4]==b'II*\x00'
            offset=struct.unpack_from('<I',data,4)[0];count=struct.unpack_from('<H',data,offset)[0];tags={}
            for i in range(count):
                pos=offset+2+i*12;tag,typ,n,value=struct.unpack_from('<HHII',data,pos)
                if typ not in (1,3,4):continue
                fmt,size={1:('B',1),3:('H',2),4:('I',4)}[typ]
                tags[tag]=struct.unpack_from('<'+fmt*n,data,pos+8 if n*size<=4 else value)
            assert tags[50706]==(1,4,0,0) and tags[262]==(32803,) and tags[258]==(16,) and tags[259]==(1,)
            sensor=b''.join(data[p:p+n] for p,n in zip(tags[273],tags[279]))
            assert hashlib.sha256(sensor).hexdigest()==result['before']['sensorSha256']
            cancelled=Path(folder)/'cancel.dng'
            assert 'error' in call('dng_probe',source=str(source),output=str(cancelled),cancelled=True)
            assert not cancelled.exists()
            assert 'error' in call('dng_probe',source=str(OUT/'sensor-copy.dng'),output=str(OUT/'sensor-copy.dng'))
            result['independentTiffSensorHash']=hashlib.sha256(sensor).hexdigest()
            result['cancelAndOriginalProtection']=True
            call('window');r=call('raw_window',path=str(OUT/'sensor-copy.dng'),recipe={},linked=True)
            assert 'error' not in r,r
            deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                call('pump',waitMs=30);r=call('raw_window_state')
                if r.get('idle') and r.get('previewPresented',0)>0:break
            assert r['recipe']['tone']['exposure']==1.25,r
            ui_path=Path(folder)/'ui-copy.dng'
            ui=call('dng_ui',output=str(ui_path),capture=str(OUT/'save-dialog.png'))
            assert ui['fileDialog'] and ui['saved'] and ui['open'],ui
            result['uiSave']=ui
            call('raw_window_state',reject=True)
            call('raw_window',path=str(ui_path),recipe={},linked=True)
            deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                call('pump',waitMs=30);r=call('raw_window_state')
                if r.get('idle') and r.get('previewPresented',0)>0:break
            ui=call('dng_ui',cancel=True)
            assert ui['fileDialog'] and not ui['saved'] and ui['open'],ui
            result['uiCancel']=ui
            (OUT/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        finally:call('shutdown')
if __name__=='__main__':main()
