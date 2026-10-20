"""Run the packaged production portrait AI worker with development PATH removed."""
import os,json,struct,subprocess,tempfile,hashlib
from pathlib import Path
from PIL import Image,ImageCms
R=Path(__file__).resolve().parents[1]
P=Path(os.environ.get('HUELITH_PACKAGE',str(R/'build/releases/M3-portrait-20261012-r1')))
env=os.environ.copy()
for key in list(env):
 if key.startswith(('QT_','HUELITH_')) or key=='PYTHONPATH':env.pop(key,None)
env['PATH']=str(Path(env.get('SystemRoot','C:/Windows'))/'System32')
with tempfile.TemporaryDirectory(prefix='huelith-m3-package-') as tmp:
 t=Path(tmp);im=Image.open(R/'research/m0/portrait-highres.jpg').convert('RGBA');im.thumbnail((900,900))
 icc=ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
 (t/'input.bin').write_bytes(struct.pack('>IiiiqI',0x48414931,im.width,im.height,17,im.width*4,len(icc))+icc+im.tobytes())
 (t/'job.json').write_text(json.dumps(dict(operation='portrait',models=str(P/'models'))),encoding='utf-8')
 result=subprocess.run([str(P/'Huelith.exe'),'--ai-worker',str(t/'job.json')],cwd=P,env=env,capture_output=True,timeout=90)
 assert result.returncode==0,(result.returncode,result.stderr)
 data=json.loads((t/'result.json').read_text(encoding='utf-8'))
 people=data.get('people',data.get('metadata',{}).get('people',[]))
 assert len(people)==1 and len(people[0]['landmarks'])==66,data.keys()
 assert people[0]['settings']['automaticMasks']['skin'] and len(people[0]['settings']['pose'])==33
 report=dict(package=str(P),passed=True,cleanPath=True,people=1,landmarks=66,pose=33,skin=True,
             executableSHA256=hashlib.sha256((P/'Huelith.exe').read_bytes()).hexdigest(),
             models=json.loads((P/'licenses/portrait-models.json').read_text()))
 out=R/'docs/test-evidence/M3/package';out.mkdir(parents=True,exist_ok=True)
 (out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 print('Packaged production executable, four portrait models and isolated worker PASS',flush=True)
