"""Clipping overlays and RGB/Lab readings through real RAW widgets and displayed pixmap."""
import ctypes,json,os,tempfile,time,traceback,re,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/raw-inspection'
def main():
    OUT.mkdir(parents=True,exist_ok=True);qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    fn=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll')).huelith_python_command
    fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    records=[]
    def test(name,f):
        try:f();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-inspection-') as folder:
        call('init',settings=folder);call('window');call('raw_window',paths=[str(ROOT/'research/m0/samples/r10-5871.CR3')],denoise=False)
        def wait():
            deadline=time.monotonic()+120
            while time.monotonic()<deadline:
                call('pump',waitMs=30);s=call('raw_window_state')
                if s['previewPresented'] and s['previewPresented']==s['previewRequested']:return s
            raise TimeoutError(s)
        wait()
        def highlight():
            call('raw_window_state',exposure=5);s=wait();assert s['highlightClippedPixels']>100
            before=s['recipe'];call('pump',waitMs=400)
            call('raw_window_state',click='rawHighlightClipping');s=call('raw_window_state',inspectPixels=True)
            assert s['displayedRedPixels']==s['highlightClippedPixels'],s
            assert s['recipe']==before
            call('raw_window_state',capture=str(OUT/'highlight.png'))
            call('raw_window_state',click='rawHighlightClipping');s=call('raw_window_state',inspectPixels=True)
            assert s['displayedRedPixels']<s['highlightClippedPixels']/2
            call('raw_window_state',click='rawUndo');assert wait()['recipe'].get('tone',{}).get('exposure',0)==0
        test('highlights_actual_red_overlay_toggle_does_not_enter_document_history',highlight)
        def shadows():
            call('raw_window_state',exposure=-5)
            call('raw_window_state',control='toneBlackPoint',value=.4);s=wait()
            call('raw_window_state',capture=str(OUT/'shadow-before.png'))
            assert s['shadowClippedPixels']>100,(s['shadowClippedPixels'],call('raw_window_state',hover=[.5,.5])['colorReadout'])
            call('raw_window_state',click='rawShadowClipping');s=call('raw_window_state',inspectPixels=True)
            assert s['displayedBluePixels']==s['shadowClippedPixels'],s
            call('raw_window_state',capture=str(OUT/'shadow.png'))
            call('raw_window_state',click='rawShadowClipping');call('raw_window_state',control='toneBlackPoint',value=0);call('raw_window_state',exposure=0);wait()
        test('shadows_actual_blue_overlay_matches_black_pixel_count',shadows)
        def readings():
            r=call('raw_window_state',hover=[.5,.5])['colorReadout'];rgb=[int(v) for v in re.findall(r'\d+',r)[:3]]
            assert len(rgb)==3,r
            call('raw_window_state',labReadoutToggle=True)
            r=call('raw_window_state',hover=[.5,.5])['colorReadout'];lab=[float(v) for v in re.findall(r'-?\d+(?:\.\d+)?',r)]
            values=[v/255 for v in rgb];values=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in values]
            matrix=[[.4360747,.3850649,.1430804],[.2225045,.7168786,.0606169],[.0139322,.0971045,.7141733]]
            xyz=[sum(c*v for c,v in zip(row,values))/white for row,white in zip(matrix,[.96422,1,.82521])]
            f=[v**(1/3) if v>.008856451679 else v*7.787037037+16/116 for v in xyz]
            expected=[116*f[1]-16,500*(f[0]-f[1]),200*(f[1]-f[2])]
            assert max(abs(a-b) for a,b in zip(lab,expected))<.06,(rgb,lab,expected)
            call('raw_window_state',capture=str(OUT/'lab-readout.png'))
        test('actual_histogram_context_lab_readout_matches_independent_reference',readings)
        call('raw_window_state',reject=True);call('shutdown')
    report=dict(passed=sum(r['passed'] for r in records),total=len(records),tests=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
