"""Actual metadata round trip, privacy policies, editable borders and Qt menu acceptance."""
import ctypes,json,os,subprocess,tempfile
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2-E/metadata'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    exe=ROOT/'build/windows-release/tools/exiftool/exiftool.exe'
    def tool(*args):
        p=subprocess.run([str(exe),'-config','',*map(str,args)],capture_output=True,timeout=30)
        assert p.returncode==0,(p.stdout,p.stderr);return p.stdout
    def read(path):return json.loads(tool('-j','-G1','-s','-n',path))[0]
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command
    fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    records=[]
    with tempfile.TemporaryDirectory(prefix='huelith-metadata-') as directory:
        folder=Path(directory);source=folder/'source.jpg';Image.new('RGB',(320,180),(80,110,150)).save(source)
        tool('-overwrite_original','-n','-Make=Canon','-Model=EOS R10','-LensModel=RF 50mm F1.8','-ExposureTime=0.004',
             '-FNumber=4','-FocalLength=50','-ISO=200','-Artist=Photographer','-Copyright=Copyright Test',
             '-IPTC:Keywords=Landscape','-XMP-dc:Title=Evening','-GPSLatitude=22.3','-GPSLatitudeRef=N',
             '-GPSLongitude=114.2','-GPSLongitudeRef=E','-XResolution=72','-YResolution=72',source)
        call('init',settings=str(folder));project=folder/'persistent.huelith'
        try:
            r=call('metadata_probe',source=str(source),project=str(project));assert 'EOS R10' in r['caption'] and '1/250' in r['caption'],r
            source.unlink() # Export cannot depend on an external file surviving.
            for fmt,bits in [('png',16),('jpg',8),('tif',16)]:
                path=OUT/f'default.{fmt}'
                call('metadata_probe',projectInput=str(project),output=str(path),format=fmt,bits=bits,size=[160,90])
                tags=read(path);assert tags['IFD0:Model']=='EOS R10' and tags['ExifIFD:ISO']==200,tags
                assert tags['IFD0:Copyright']=='Copyright Test' and tags['IPTC:Keywords']=='Landscape' and tags['XMP-dc:Title']=='Evening',tags
                assert not any(k.startswith('GPS:') for k in tags),tags
                assert tags['ExifIFD:ExifImageWidth']==160 and tags['ExifIFD:ExifImageHeight']==90 and tags['IFD0:Orientation']==1,tags
                assert abs(Image.open(path).info.get('dpi',(300,300))[0]-300)<1,tags
                assert Image.open(path).info.get('icc_profile'),tags
                records.append(dict(case=f'project-retains-metadata-source-deleted-{fmt}-{bits}',passed=True))
            for name,flags in [('none',[False]*5),('rights',[False,False,False,True,False]),('gps',[False,False,False,False,True]),('all',[True]*5)]:
                path=OUT/f'{name}.jpg';call('metadata_probe',projectInput=str(project),output=str(path),format='jpg',flags=flags)
                tags=read(path)
                assert ('IFD0:Copyright' in tags)==flags[3],tags
                assert ('IFD0:Model' in tags)==flags[0],tags
                assert ('GPS:GPSLatitude' in tags)==flags[4],tags
                assert ('IPTC:Keywords' in tags)==flags[1] and ('XMP-dc:Title' in tags)==flags[2],tags
                records.append(dict(case='policy-'+name,passed=True))
            border=OUT/'border.huelith';r=call('metadata_probe',projectInput=str(project),border=True,project=str(border),output=str(OUT/'border.png'))
            assert r['width']==344 and r['height']==254 and r['layers']==3 and r['editableText'],r
            r=call('metadata_probe',projectInput=str(border));assert r['editableText'] and r['metadata']['IFD0:Model']=='EOS R10',r
            records.append(dict(case='border-preserves-editable-source-and-text-after-reopen',passed=True))
            logo=folder/'logo.png';Image.new('RGBA',(48,48),(200,80,20,140)).save(logo)
            r=call('metadata_probe',projectInput=str(project),border=True,logo=str(logo),output=str(OUT/'border-logo.png'))
            assert r['layers']==4 and r['editableText'],r
            call('window');call('window_action',text='打开…',dialog={'path':str(OUT/'default.jpg')})
            r=call('border_ui',capture=str(OUT/'border-dialog.png'));assert r['shown'] and r['after']==r['before']+1 and r['editableText'],r
            r=call('border_ui',cancel=True);assert r['shown'] and r['after']==r['before'],r
            r=call('border_ui',batch=True);assert r['shown'] and r['after']==r['before']*2,r
            quick=folder/'quick.png'
            call('window_action',text='导出 PNG / JPEG…',dialog={'accept':True,'path':str(quick),'exportMetadata3':False,'capture':str(OUT/'export-dialog.png')})
            tags=read(quick);assert tags['IFD0:Model']=='EOS R10' and 'IFD0:Copyright' not in tags,tags
            records.append(dict(case='single-export-dialog-honors-metadata-policy',passed=True))
            records.append(dict(case='actual-menu-create-and-cancel-border',passed=True))
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
    print(json.dumps(records),flush=True)
if __name__=='__main__':main()
