"""Real output files: five encodings x four ICC spaces, transparency, queue and presets."""
import ctypes,io,json,os,subprocess,sys,tempfile,time,traceback
from pathlib import Path
import cv2
import numpy as np
from PIL import Image,ImageCms
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/export-matrix'
def main():
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64');handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows';OUT.mkdir(parents=True,exist_ok=True)
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    def wait(window=False):
        deadline=time.monotonic()+60
        while time.monotonic()<deadline:
            r=call('export_queue',window=window)['items']
            if all(i['status']>=3 for i in r):return r
            call('pump',waitMs=20)
        raise TimeoutError(r)
    def gui(**kw):return call('export_queue',window=True,**kw)['windowState']
    def scene(color='#4060a0',opacity=.37):call('window_scene',width=64,height=32,layers=[dict(name='fixture',color=color,opacity=opacity)])
    results=[];metrics=[]
    if len(sys.argv)>2 and sys.argv[1]=='--reopen':
        call('init',settings=sys.argv[2]);call('window');scene();call('window_action',text='批量导出与队列…')
        print(json.dumps(gui(),ensure_ascii=True));call('shutdown');return 0
    def test(name,body):
        try:body();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(results[-1],flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-export-matrix-') as folder:
        folder=Path(folder)
        call('init',settings=str(folder));call('window');scene()
        try:
            def matrix():
                call('export_queue',reset=True)
                combinations=[(fmt,bits,profile) for fmt,bits in [('png',8),('png',16),('tif',8),('tif',16),('jpg',8)] for profile in range(4)]
                for fmt,bits,profile in combinations:call('export_queue',path=str(folder/f'{fmt}-{bits}-{profile}.{fmt}'),format=fmt,bits=bits,profile=profile,quality=100)
                call('export_queue',start=True);items=wait();assert all(i['status']==3 for i in items),items
                srgb=ImageCms.createProfile('sRGB');rgb=Image.new('RGB',(1,1),(64,96,160))
                for fmt,bits,profile in combinations:
                    path=folder/f'{fmt}-{bits}-{profile}.{fmt}'
                    with Image.open(path) as image:
                        assert image.size==(64,32);icc=image.info['icc_profile'];output_profile=ImageCms.ImageCmsProfile(io.BytesIO(icc))
                        expected=np.array(ImageCms.profileToProfile(rgb,srgb,output_profile,outputMode='RGB'))[0,0].astype(float)
                        if fmt=='jpg':expected=expected*.37+255*.63
                        actual=np.array(image.convert('RGBA'))[10,20];error=float(np.max(abs(actual[:3]-expected)))
                        assert error<=3,(fmt,bits,profile,actual,expected,error)
                        assert abs(int(actual[3])-(255 if fmt=='jpg' else round(255*.37)))<=1
                        if fmt=='tif':assert tuple(image.tag_v2[258])==(bits,)*4
                        if fmt=='png':assert path.read_bytes()[24]==bits
                        metrics.append(dict(format=fmt,bits=bits,profile=profile,error8bit=error,iccBytes=len(icc)))
                    if bits==16:
                        pixels=cv2.imdecode(np.frombuffer(path.read_bytes(),np.uint8),cv2.IMREAD_UNCHANGED);assert pixels.dtype==np.uint16
                        assert abs(int(pixels[10,20,3])-round(65535*.37))<=1
                        assert int(pixels[10,20,3])%257!=0,'alpha truncated to 8 bits'
            test('twenty_encoded_files_littlecms_reference_icc_depth_alpha_precision',matrix)
            def flattened():
                scene();call('export_queue',reset=True)
                for fmt in ['png','tif','jpg']:call('export_queue',path=str(folder/f'matte.{fmt}'),format=fmt,bits=8,alpha=False,matte='#204080',quality=100)
                call('export_queue',start=True);assert all(i['status']==3 for i in wait())
                expected=np.array([64,96,160])*.37+np.array([32,64,128])*.63
                for fmt in ['png','tif','jpg']:
                    actual=np.array(Image.open(folder/f'matte.{fmt}').convert('RGBA'))[8,8];assert np.max(abs(actual[:3]-expected))<=2,(actual,expected);assert actual[3]==255
            test('custom_matte_consistency_png_tiff_jpeg',flattened)
            def presets():
                call('window_action',text='批量导出与队列…');r=gui(exportQueuePreset=3)
                assert r['exportQueueFormat']['index']==4 and r['exportQueueProfile']['index']==3
                r=gui(exportQueuePresetName='测试预设',exportQueueFormat=1,exportQueueProfile=1,exportQueueSizes='32, 64',exportQueueNaming='{name}-{index}-{size}',exportQueueQuality=83,exportQueueAlpha=False,click='exportQueuePresetSave')
                assert r['exportQueuePreset']['items'].count('测试预设')==1
                index=r['exportQueuePreset']['index'];gui(exportQueuePreset=1);r=gui(exportQueuePreset=index)
                assert r['exportQueueSizes']=='32, 64' and r['exportQueueQuality']==83 and r['exportQueueProfile']['index']==1 and not r['alpha']
                r=gui(exportQueueQuality=91,click='exportQueuePresetSave');assert r['exportQueuePreset']['items'].count('测试预设')==1
                output=folder/'preset-output';output.mkdir();gui(exportQueueDirectory=str(output),click='exportQueueStart');items=wait(True);assert len(items)==2 and all(i['status']==3 for i in items)
                gui(capture=str(OUT/'export-presets.png'))
                settings=next(folder.rglob('*.ini'));text=settings.read_text(encoding='utf-8');assert 'lastSettings' in text and 'presets' in text
                process=subprocess.run([sys.executable,str(Path(__file__).resolve()),'--reopen',str(folder)],capture_output=True,timeout=30)
                assert process.returncode==0,process.stderr.decode(errors='replace')
                reopened=json.loads(process.stdout);assert '测试预设' in reopened['exportQueuePreset']['items'] and reopened['exportQueueQuality']==91 and reopened['exportQueueProfile']['index']==1 and reopened['exportQueueSizes']=='32, 64'
                r=gui(click='exportQueuePresetDelete');assert '测试预设' not in r['exportQueuePreset']['items']
            test('actual_preset_apply_save_update_delete_and_multisize_encoding',presets)
            def stress():
                call('export_queue',reset=True);start=time.perf_counter()
                for i in range(80):
                    scene('#204080' if i%2 else '#a08040',.5)
                    call('export_queue',path=str(folder/f'snapshot-{i}.png'),width=32,height=16)
                scene('#ffffff',1);call('export_queue',start=True);items=wait();assert len(items)==80 and all(i['status']==3 for i in items)
                for i in range(80):
                    p=np.array(Image.open(folder/f'snapshot-{i}.png'))[3,4];expected=[32,64,128] if i%2 else [160,128,64]
                    assert np.max(abs(p[:3].astype(int)-expected))<=1 and p[3]==128
                metrics.append(dict(queueJobs=80,elapsedSeconds=time.perf_counter()-start))
            test('eighty_snapshot_jobs_survive_document_changes_no_partial_files',stress)
            def composite():
                # Six megapixel nested document: transforms, soft masks, opacity,
                # a persistent Gaussian effect and editable Unicode text.
                yy,xx=np.mgrid[:2000,:3000]
                base=np.empty((2000,3000,3),np.uint8)
                base[:,:,0]=(xx*255//2999);base[:,:,1]=(yy*255//1999);base[:,:,2]=90
                base_path=folder/'large-source.png';Image.fromarray(base).save(base_path)
                overlay=np.zeros((700,1000,4),np.uint8);overlay[:]=[210,60,100,170]
                overlay[:,:,3]=(np.linspace(0,200,1000)[None,:]).astype(np.uint8)
                overlay_path=folder/'alpha.png';Image.fromarray(overlay).save(overlay_path)
                call('window_scene',width=3000,height=2000,layers=[dict(name='background',path=str(base_path)),dict(name='masked group',kind=5,mask=180,opacity=.8,x=120,y=80,children=[dict(name='alpha',path=str(overlay_path),x=150,y=100,scale=1.3),dict(name='nested',kind=5,opacity=.6,x=900,y=700,children=[dict(name='inner',path=str(overlay_path),scale=.9)])])])
                call('window_action',text='高斯模糊…',dialog=dict(accept=True,steps=[dict(control='effectBlurRadius',value=3.5)]))
                call('window_control',name='toolButton_25',value=True)
                call('window_mouse',phase='press',x=350,y=1400);call('window_mouse',phase='release',x=350,y=1400)
                call('window_design',textInput='Huelith · 复杂合成输出');call('window_design',key='Ctrl+A');call('window_design',control='textSize',value=72);call('window_design',key='Ctrl+Return')
                expected_path=folder/'preview-reference.png';call('window_brush',pixels=str(expected_path))
                project=folder/'complex.huelith';call('window_design',file=str(project))
                reference=call('window_brush')['hash'];call('window_open',path=str(project));assert call('window_brush')['hash']==reference
                call('export_queue',reset=True)
                jobs=[('png',8,0,3000,2000),('png',16,1,1500,1000),('tif',16,3,3000,2000),('jpg',8,0,1500,1000)]
                for i,(fmt,bits,profile,w,h) in enumerate(jobs):call('export_queue',path=str(folder/f'complex-{i}.{fmt}'),format=fmt,bits=bits,profile=profile,width=w,height=h,quality=100)
                start=time.perf_counter();call('export_queue',start=True);scene('#ffffff',1)
                heartbeats=[];deadline=time.monotonic()+90
                while time.monotonic()<deadline:
                    tick=time.perf_counter();call('pump',waitMs=10);heartbeats.append((time.perf_counter()-tick)*1000)
                    items=call('export_queue')['items']
                    if all(i['status']>=3 for i in items):break
                assert all(i['status']==3 for i in items),items
                actual=np.asarray(Image.open(folder/'complex-0.png').convert('RGBA')).astype(int)
                expected=np.asarray(Image.open(expected_path).convert('RGBA')).astype(int)
                difference=abs(actual-expected);assert difference.max()<=2,int(difference.max())
                for i,(fmt,bits,profile,w,h) in enumerate(jobs):
                    with Image.open(folder/f'complex-{i}.{fmt}') as image:
                        assert image.size==(w,h) and image.info.get('icc_profile')
                        if fmt=='tif':assert tuple(image.tag_v2[258])==(16,)*4
                assert max(heartbeats)<250,max(heartbeats)
                with Image.open(folder/'complex-0.png') as image:image.thumbnail((1000,1000));image.save(OUT/'complex-document.png')
                metrics.append(dict(compositePixels=6000000,jobs=len(jobs),elapsedSeconds=time.perf_counter()-start,maxEventMs=max(heartbeats),maxPreviewExportError=int(difference.max())))
            test('six_megapixel_nested_masks_effects_unicode_project_and_immutable_color_outputs',composite)
            def large_queue():
                from ctypes import wintypes
                class Counters(ctypes.Structure):
                    _fields_=[('cb',wintypes.DWORD),('faults',wintypes.DWORD)]+[(name,ctypes.c_size_t) for name in ('peakWorkingSet','workingSet','peakPaged','paged','peakNonPaged','nonPaged','pagefile','peakPagefile')]
                kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=wintypes.HANDLE
                psapi=ctypes.WinDLL('psapi',use_last_error=True);psapi.GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(Counters),wintypes.DWORD]
                def memory():
                    info=Counters();info.cb=ctypes.sizeof(info)
                    if not psapi.GetProcessMemoryInfo(kernel.GetCurrentProcess(),ctypes.byref(info),info.cb):raise ctypes.WinError(ctypes.get_last_error())
                    return info.workingSet
                # Full camera-sized immutable source. Deterministic texture avoids a
                # uniform-color shortcut in the encoder while keeping the fixture small.
                y,x=np.indices((4000,6000),dtype=np.uint16)
                data=np.empty((4000,6000,3),np.uint8)
                data[:,:,0]=(x//8+y//16)%256;data[:,:,1]=(x//29+y//11)%256;data[:,:,2]=(x//19+y//23)%256
                source=folder/'24mp-source.png';Image.fromarray(data).save(source);del x,y,data
                call('window_scene',width=6000,height=4000,layers=[dict(name='24MP immutable',path=str(source))])
                call('export_queue',reset=True)
                jobs=[('png',8,0),('tif',16,3),('jpg',8,1)]*2
                for i,(fmt,bits,profile) in enumerate(jobs):
                    call('export_queue',path=str(folder/f'24mp-{i}.{fmt}'),format=fmt,bits=bits,profile=profile,width=6000,height=4000,quality=95)
                before=memory();peak=before;start=time.perf_counter();call('export_queue',start=True)
                heartbeat=[];deadline=time.monotonic()+180
                while time.monotonic()<deadline:
                    tick=time.perf_counter();call('pump',waitMs=10);heartbeat.append((time.perf_counter()-tick)*1000)
                    peak=max(peak,memory());items=call('export_queue')['items']
                    if all(i['status']>=3 for i in items):break
                assert all(i['status']==3 for i in items),items
                for i,(fmt,bits,profile) in enumerate(jobs):
                    with Image.open(folder/f'24mp-{i}.{fmt}') as image:
                        assert image.size==(6000,4000) and image.info.get('icc_profile')
                        if fmt=='tif':assert tuple(image.tag_v2[258])==(16,)*4
                for first,second in [(0,3),(1,4),(2,5)]:
                    assert (folder/f'24mp-{first}.{jobs[first][0]}').read_bytes()==(folder/f'24mp-{second}.{jobs[second][0]}').read_bytes()
                assert max(heartbeat)<250,max(heartbeat)
                assert peak-before<1800*1024*1024,(before,peak)
                metrics.append(dict(largeSourcePixels=24000000,jobs=6,elapsedSeconds=time.perf_counter()-start,maxEventMs=max(heartbeat),baselineMiB=before/2**20,peakMiB=peak/2**20,additionalMiB=(peak-before)/2**20))
                cancellations=[]
                for phase in [1,2]:
                    target=folder/f'cancel-phase-{phase}.png'
                    call('export_queue',reset=True,path=str(target),format='png',bits=16,profile=3,width=6000,height=4000)
                    call('export_queue',start=True);deadline=time.monotonic()+30
                    while time.monotonic()<deadline:
                        item=call('export_queue')['items'][0]
                        if item['status']==phase:break
                        assert item['status']<3,item
                        call('pump',waitMs=5)
                    assert item['status']==phase,item
                    tick=time.perf_counter();call('export_queue',cancelAll=True)
                    item=wait()[0];elapsed=(time.perf_counter()-tick)*1000
                    assert item['status']==5 and not target.exists(),item
                    assert elapsed<1000,(phase,elapsed)
                    assert not list(folder.glob('.huelith-export-*'))
                    cancellations.append(dict(phase=phase,cancelMs=elapsed))
                metrics.append(dict(largeCancellation=cancellations))

            test('24mp_six_full_resolution_jobs_bounded_memory_repeatable_files',large_queue)

        finally:call('shutdown')
    report=dict(passed=sum(t['passed'] for t in results),total=len(results),tests=results,metrics=metrics)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
