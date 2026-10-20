"""Confirm a candidate through the UI and verify its non-circular coverage persists."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import base64,io,cv2,sys
os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe')
supplemented='--supplemented' in sys.argv
independent='--independent' in sys.argv or supplemented
OUT=ROOT/'docs/test-evidence/M3/blemish-model'/('supplemented-footprint' if supplemented else 'independent-footprint' if independent else 'footprint');OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/acne-face-source.png'
recipe=json.loads(source.with_name('acne-face-recipe.json').read_text());recipe['people'][0]['settings']['repairs']=[]
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=25);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='init',settings=str(OUT/'settings'))
if independent:
    analysis=call(op='portrait_geometry',source=str(source),person=recipe['people'][0],repairAnalysis=True,blemishEvidence=str(OUT.parent/'acne-face-native.png'))
    recipe['people'][0]['settings']['blemishes']=analysis['blemishes']
call(op='portrait_window',path=str(source),recipe=recipe,models=str(ROOT/'build/windows-release/models'));wait()
call(op='portrait_state',select=0,panel=7);wait()
if not independent:call(op='portrait_state',click='portraitAnalyzeRepairs')
before=wait()['recipe']
marks=before['people'][0]['settings']['blemishes'];assert marks and all('coverage' in q for q in marks)
index=next(i for i,q in enumerate(marks) if q['analysisVersion']==4) if independent else 0
if supplemented:index=next(i for i,q in enumerate(marks) if q.get('supplementedSkinPixels',0)>0)
call(op='portrait_state',source=str(source),render=str(OUT/'before.png'))
call(op='portrait_state',blemishSelect=index)
outline=call(op='portrait_state',suggestionOutline=True,capture=str(OUT/'selected.png'))['suggestionOutline']
assert outline['elements']>20,'selection still shows a generic circle'
call(op='portrait_state',click='portraitApplyBlemish');after=wait()['recipe']
settings=after['people'][0]['settings'];assert settings['blemishes'][index]['state']=='repaired'
stroke=settings['repairs'][-1];coverage=settings['masks']['repair.item.'+stroke['id']];assert coverage==marks[index]['coverage']
assert stroke['validatedCandidateCoverage']
mask=np.asarray(Image.open(io.BytesIO(base64.b64decode(coverage))))
q=marks[index];yy,xx=np.mgrid[:mask.shape[0],:mask.shape[1]]
circle=(xx-q['x']*mask.shape[1])**2+(yy-q['y']*mask.shape[0])**2<=(q['radius']*min(mask.shape))**2
assert 0<np.count_nonzero(mask)<np.count_nonzero(circle)
r=call(op='portrait_state',source=str(source),render=str(OUT/'after.png'),save=str(OUT/'confirmed.huelith'),checkRegion=True)
assert r['roundtrip'] and r['regionError']<2e-6
a=np.asarray(Image.open(OUT/'before.png')).astype(float);b=np.asarray(Image.open(OUT/'after.png')).astype(float)
allowed=cv2.resize(mask,(a.shape[1],a.shape[0]),interpolation=cv2.INTER_LINEAR)>0
allowed=cv2.dilate(allowed.astype(np.uint8),np.ones((5,5),np.uint8))>0
delta=np.abs(a-b);assert delta[allowed].max()>0 and delta[~allowed].max()==0
if supplemented:
    call(op='portrait_geometry',recipe=before,styleMask='skin',size=[a.shape[1],a.shape[0]],mask=str(OUT/'original-skin.png'))
    skin=np.asarray(Image.open(OUT/'original-skin.png'))
    assert np.any(allowed&(skin<=230))
    assert delta[allowed&(skin<=230)].max()>0,'confirmed repair lost its supplemented skin pixels'
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
if supplemented:
    # Explicit user protection remains authoritative over validated candidate coverage.
    import copy
    protected=copy.deepcopy(after);buffer=io.BytesIO();Image.new('L',(64,64),255).save(buffer,format='PNG')
    protected['people'][0]['settings']['masks']['repair.preserve']=base64.b64encode(buffer.getvalue()).decode()
    call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=protected,models=str(ROOT/'build/windows-release/models'));wait()
    call(op='portrait_state',source=str(source),render=str(OUT/'protected.png'))
    assert np.array_equal(np.asarray(Image.open(OUT/'protected.png')).astype(float),a),'validated coverage bypassed user protection'
(OUT/'results.json').write_text(json.dumps(dict(footprintPixels=int(np.count_nonzero(mask)),circlePixels=int(np.count_nonzero(circle)),outsideChange=float(delta[~allowed].max()),insideChange=float(delta[allowed].max()),undoRedo=True,roundtrip=True),indent=2))
call(op='portrait_close');call(op='shutdown');print('Confirmed footprint retained; changes confined to coverage; undo/redo and save passed.')
