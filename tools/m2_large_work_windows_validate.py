"""M0 Python/production-DLL harness: large images, real AI workers and Windows UI."""
import argparse,ctypes,json,os,tempfile,time,threading,zipfile,hashlib
from pathlib import Path
from ctypes import wintypes
from PIL import Image
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/large-work-windows'
def memory_usage():
    class Entry(ctypes.Structure):
        _fields_=[('size',wintypes.DWORD),('usage',wintypes.DWORD),('pid',wintypes.DWORD),('heap',ctypes.c_size_t),('module',wintypes.DWORD),('threads',wintypes.DWORD),('parent',wintypes.DWORD),('priority',wintypes.LONG),('flags',wintypes.DWORD),('name',wintypes.WCHAR*260)]
    class Memory(ctypes.Structure):
        _fields_=[('cb',wintypes.DWORD),('faults',wintypes.DWORD),*[(n,ctypes.c_size_t) for n in ['peak','rss','peakPaged','paged','peakNonpaged','nonpaged','pagefile','peakPagefile','private']]]
    api=ctypes.WinDLL('kernel32',use_last_error=True)
    api.CreateToolhelp32Snapshot.restype=wintypes.HANDLE;api.OpenProcess.restype=wintypes.HANDLE
    api.Process32FirstW.argtypes=[wintypes.HANDLE,ctypes.POINTER(Entry)];api.Process32NextW.argtypes=api.Process32FirstW.argtypes
    api.CloseHandle.argtypes=[wintypes.HANDLE];api.K32GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(Memory),wintypes.DWORD]
    snapshot=api.CreateToolhelp32Snapshot(2,0);entry=Entry();entry.size=ctypes.sizeof(entry);pids={os.getpid()}
    parents={};ok=api.Process32FirstW(snapshot,ctypes.byref(entry))
    while ok:parents[entry.pid]=entry.parent;ok=api.Process32NextW(snapshot,ctypes.byref(entry))
    api.CloseHandle(snapshot)
    for _ in range(3):pids|={p for p,parent in parents.items() if parent in pids}
    rss=private=0
    for pid in pids:
        handle=api.OpenProcess(0x410,False,pid)
        if not handle:continue
        memory=Memory();memory.cb=ctypes.sizeof(memory)
        if api.K32GetProcessMemoryInfo(handle,ctypes.byref(memory),memory.cb):rss+=memory.rss;private+=memory.private
        api.CloseHandle(handle)
    return rss,private
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--case',choices=['storage','huge','noise','super','windows','panorama','compat'],required=True);args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        value=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert not value.get('error'),value;return value
    stop=threading.Event();peak={'rss':0,'private':0}
    def monitor():
        while not stop.wait(.2):
            rss,private=memory_usage()
            peak['rss']=max(peak['rss'],rss);peak['private']=max(peak['private'],private)
    thread=threading.Thread(target=monitor);thread.start()
    started=time.perf_counter()
    try:
        with tempfile.TemporaryDirectory(prefix='huelith-large-') as directory:
            call('init',settings=directory)
            if args.case in ['storage','noise','huge']:
                path=Path(directory)/'large.huelith'
                size=(24000,16000) if args.case=='huge' else (12000,8000)
                kw={} if args.case in ['noise','huge'] else {'export':str(OUT/'96MP.tif')}
                result=call('large_image_probe',width=size[0],height=size[1],noise=args.case=='noise',project=str(path),**kw)
                assert result['width']*result['height']>64000000 and result['pixelEdit'] and result['undoRedo'],result
                with zipfile.ZipFile(path) as z:
                    manifest=json.loads(z.read('manifest.json'));assert manifest['schema']==15
                    assert z.testzip() is None
                if args.case=='noise':assert result['fileBytes']>1024**3,result
                if args.case=='storage':
                    with Image.open(OUT/'96MP.tif') as im:assert im.size==size
                result['independentZipValidation']=True
            elif args.case=='windows':
                call('window');result={}
                for kind in ['raw','enhance','merge']:
                    check=call('work_window_probe',kind=kind,source=str(ROOT/'research/m0/samples/r10-5871.CR3'),capture=str(OUT/f'{kind}-independent.png'))
                    assert check['stages']==4 and all(check[k] for k in ['nonModal','unowned','mainEnabled','childMinimized','mainNotMinimized','mainEdited','childRestored','mainVisible','mainMinimizedIndependently']),check
                    result[kind]=check
            elif args.case=='super':
                y,x=np.indices((2048,2048));pixels=np.empty((2048,2048,3),np.uint8)
                pixels[...,0]=(x//8)%256;pixels[...,1]=(y//8)%256;pixels[...,2]=((x+y)//16)%256
                source=OUT/'super-input.png';Image.fromarray(pixels).save(source)
                result=call('enhancement_probe',source=str(source),scale=4,directml=True,output=str(OUT/'super-8192.png'))
                assert result['width']==8192 and result['height']==8192 and result['workerIsolated'],result
                with Image.open(OUT/'super-8192.png') as im:
                    assert im.size==(8192,8192)
                    im.resize((1024,1024)).save(OUT/'super-overview.png')
                    a=np.asarray(im.crop((360,360,820,820))).astype(float)
                    seam=np.abs(np.diff(a,axis=1)).mean(axis=(0,2));assert seam.max()<6, float(seam.max())
                result['maxLocalColumnDifference']=float(seam.max())
            elif args.case=='compat':
                legacy=ROOT/'docs/test-evidence/M2/gaussian/gaussian.huelith'
                original=hashlib.sha256(legacy.read_bytes()).hexdigest()
                with zipfile.ZipFile(legacy) as z:version=json.loads(z.read('manifest.json'))['schema'];assert version<15
                upgraded=Path(directory)/'upgraded.huelith'
                result=call('metadata_probe',projectInput=str(legacy),project=str(upgraded))
                call('metadata_probe',projectInput=str(upgraded))
                assert original==hashlib.sha256(legacy.read_bytes()).hexdigest()
                with zipfile.ZipFile(upgraded) as z:info=z.getinfo('document.bin')
                with upgraded.open('r+b') as f:
                    f.seek(info.header_offset+30);name_extra=f.read(len(info.filename.encode())+20)
                    position=f.tell()+32;f.seek(position);value=f.read(1);f.seek(position);f.write(bytes([value[0]^1]))
                rejected=json.loads(fn(json.dumps(dict(op='metadata_probe',projectInput=str(upgraded))).encode()))
                assert 'error' in rejected,rejected
                result.update(legacySchema=version,legacyUnchanged=True,corruptionRejected=True)
                imports={}
                for name in ['super-8192.png','96MP.tif']:
                    check=call('metadata_probe',source=str(OUT/name));assert check['width']*check['height']>64000000;imports[name]=[check['width'],check['height']]
                jpeg=Path(directory)/'96MP.jpg'
                Image.new('RGB',(12000,8000),(90,120,160)).save(jpeg)
                check=call('metadata_probe',source=str(jpeg));assert check['width']==12000 and check['height']==8000
                imports['96MP.jpg']=[12000,8000];result['imports']=imports
                call('window');call('window_open',path=str(OUT/'96MP.tif'))
                call('window_brush',exposure=.25)
                deadline=time.monotonic()+120
                while time.monotonic()<deadline:
                    check=call('window_brush',previewState=True);assert not check['error'],check
                    if not check['pending']:break
                    call('pump',waitMs=30)
                assert not check['pending'],check
                call('window_capture',path=str(OUT/'96MP-editor.png'));result['editorPreview']=check
            else:
                sources=[ROOT/'research/m2/merge'/f'a{i}.png' for i in (1,2,3)]
                if len(sources)<3:raise RuntimeError(f'Need known panorama fixtures: {sources}')
                paths=[]
                for i,source in enumerate(sources[:3]):
                    p=Path(directory)/f'pano-{i}.tif'
                    with Image.open(source) as im:im.convert('RGB').resize((im.width*12,im.height*12)).save(p)
                    paths.append(str(p))
                result=call('photo_merge_probe',mode=1,paths=paths,output=str(OUT/'large-panorama.png'))
                assert result['width']*result['height']>64000000,result
            call('shutdown')
    finally:
        stop.set();thread.join()
    result.update(case=args.case,seconds=time.perf_counter()-started,peakBytes=peak)
    (OUT/f'{args.case}.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=True),flush=True)
if __name__=='__main__':main()
