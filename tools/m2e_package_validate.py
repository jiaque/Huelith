"""Exercise the shipped worker/model and metadata utility with no development PATH."""
import json,os,struct,subprocess,tempfile
from pathlib import Path
from PIL import ImageCms
ROOT=Path(__file__).resolve().parents[1]
PACKAGE=Path(os.environ.get('HUELITH_PACKAGE',str(ROOT/'build/releases/M2-large-work-windows-20261012-r25')))
def main():
    environment=os.environ.copy()
    for key in list(environment):
        if key.startswith(('QT_','HUELITH_')) or key=='PYTHONPATH':environment.pop(key,None)
    environment['PATH']=str(Path(environment.get('SystemRoot','C:/Windows'))/'System32')
    with tempfile.TemporaryDirectory(prefix='huelith-package-') as directory:
        path=Path(directory);icc=ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
        (path/'input.bin').write_bytes(struct.pack('>IiiiqI',0x48414931,32,24,17,128,len(icc))+icc+bytes((90,120,140,255))*(32*24))
        (path/'job.json').write_text(json.dumps(dict(operation='enhance',mode=0,scale=2,strength=1,directml=False,quality=False)),encoding='utf-8')
        result=subprocess.run([str(PACKAGE/'Huelith.exe'),'--ai-worker',str(path/'job.json')],cwd=PACKAGE,env=environment,capture_output=True,timeout=60)
        assert result.returncode==0,(result.returncode,result.stderr)
        metadata=json.loads((path/'result.json').read_text(encoding='utf-8'))
        magic,width,height=struct.unpack('>Iii',(path/'output.bin').read_bytes()[:12]);assert magic==0x48414931 and (width,height)==(64,48)
        exif=subprocess.run([str(PACKAGE/'tools/exiftool/exiftool.exe'),'-config','','-j','-Model',str(ROOT/'docs/test-evidence/M2-E/metadata/default.jpg')],cwd=PACKAGE,env=environment,capture_output=True,timeout=30)
        assert exif.returncode==0,(exif.returncode,exif.stderr)
        assert json.loads(exif.stdout)[0]['Model']=='EOS R10'
    report=dict(package=str(PACKAGE),isolatedWorker=True,model=metadata,metadataUtility=True,outputSize=[width,height])
    out=ROOT/'docs/test-evidence/M2-E/package';out.mkdir(parents=True,exist_ok=True)
    (out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))
if __name__=='__main__':main()
