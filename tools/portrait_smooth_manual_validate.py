from pathlib import Path
import os,io,base64
temp=Path.cwd()/"build/portrait-ai-tmp";temp.mkdir(exist_ok=True);os.environ["TMP"]=os.environ["TEMP"]=str(temp);os.environ["HUELITH_AI_WORKER_EXE"]=str(Path.cwd()/"build/windows-release/Huelith.exe")
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-smooth-manual';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'workflow-settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png'
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])

recipe['people'][0]['settings']['parameters']={'skin.smooth':70}
call(op='portrait_window',path=str(source),recipe=recipe,models=str(ROOT/'build/windows-release/models'));wait();call(op='portrait_state',select=0,panel=5);wait();call(op='portrait_state',smoothMode=1);manual=wait()['recipe']
call(op='portrait_state',source=str(source),render=str(OUT/'empty.png'));r=call(op='portrait_geometry',recipe=manual,size=list(Image.open(source).size),source=str(source),render=str(OUT/'empty-core.png'),colorMetrics=True);assert r['changedPixels']==0,r
start=[.4,.45];end=[.5,.45]
call(op='portrait_state',brushSize=35);call(op='portrait_state',paintPath=[start,end]);sparse=wait()['recipe'];mask=sparse['people'][0]['settings']['smoothManualMasks']['skin.smooth']
auto=json.loads(json.dumps(sparse));auto['people'][0]['settings']['smoothManual']['skin.smooth']=False
call(op='portrait_geometry',recipe=auto,size=[768,480],styleMask='skin.smooth',mask=str(OUT/'automatic.png'))
painted=np.array(Image.open(io.BytesIO(base64.b64decode(mask))));automatic=np.array(Image.open(OUT/'automatic.png').resize((painted.shape[1],painted.shape[0])));assert np.count_nonzero((painted>150)&(automatic<20))>10

call(op='portrait_state',source=str(source),render=str(OUT/'painted.png'));assert np.max(abs(np.array(Image.open(OUT/'empty.png')).astype(int)-np.array(Image.open(OUT/'painted.png')).astype(int)))>0
call(op='portrait_state',history=-1);assert wait()['recipe']==manual
call(op='portrait_state',paintPath=[start]*20+[end]*20);dense=wait()['recipe'];assert dense['people'][0]['settings']['smoothManualMasks']['skin.smooth']==mask
call(op='portrait_state',smoothMode=0);wait();call(op='portrait_state',smoothMode=1);assert wait()['recipe']['people'][0]['settings']['smoothManualMasks']['skin.smooth']==mask
call(op='portrait_state',smoothTarget=1);call(op='portrait_state',smoothMode=1);wait();call(op='portrait_state',paint=[.55,.6]);body=wait()['recipe'];assert body['people'][0]['settings']['smoothManualMasks']['skin.smooth']==mask and 'skin.bodySmooth' in body['people'][0]['settings']['smoothManualMasks']
call(op='portrait_state',smoothTarget=0);wait();before=wait()['recipe'];call(op='portrait_state',click='portraitSmoothErase');call(op='portrait_state',paintPath=[start,end]);erased=wait()['recipe'];assert erased['people'][0]['settings']['smoothManualMasks']['skin.bodySmooth']==body['people'][0]['settings']['smoothManualMasks']['skin.bodySmooth'];assert erased['people'][0]['settings']['smoothManualMasks']['skin.smooth']!=mask
call(op='portrait_state',history=-1);assert wait()['recipe']==before
r=call(op='portrait_state',source=str(source),render=str(OUT/'both.png'),checkRegion=True,save=str(OUT/'manual.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
saved=wait()['recipe'];call(op='portrait_state',detect=True);redetected=wait()['recipe'];assert any(q['settings'].get('smoothManualMasks')==saved['people'][0]['settings']['smoothManualMasks'] for q in redetected['people']);call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=saved);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'both.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Empty manual start, event-invariant path, separate face/body, mode preservation, erase/undo and save passed')
