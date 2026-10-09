"""Create the M1 local portable package; no test executable is generated."""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import hashlib

ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,default=ROOT/'build/releases/M1-local')
    args=parser.parse_args()
    destination=args.output.resolve()
    if not destination.is_relative_to((ROOT/'build/releases').resolve()):
        raise ValueError('Package output must stay inside build/releases')
    destination.mkdir(parents=True,exist_ok=True)
    app=ROOT/'build/windows-release/Huelith.exe'
    shutil.copy2(app,destination/app.name)
    qt=Path(r'D:\Program Files\Qt\6.10.3\msvc2022_64')
    result=subprocess.run([str(qt/'bin/windeployqt.exe'),'--release','--no-translations',str(destination/app.name)],capture_output=True,text=True)
    evidence=ROOT/'docs/test-evidence/M1';evidence.mkdir(parents=True,exist_ok=True)
    (evidence/'package-deploy.log').write_text(result.stdout+'\n'+result.stderr,encoding='utf-8')
    if result.returncode:
        raise RuntimeError('Qt deployment failed; inspect package-deploy.log')
    report={'path':str(destination),'exe_sha256':hashlib.sha256((destination/app.name).read_bytes()).hexdigest(),'deployment_exit':result.returncode}
    try:
        check=subprocess.run([str(destination/app.name),'--smoke-test'],cwd=destination,capture_output=True,text=True,timeout=30)
        report.update(startup_exit=check.returncode,stderr=check.stderr)
    except (OSError,subprocess.TimeoutExpired) as error:
        report.update(startup_error=str(error))
    (evidence/'package.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
