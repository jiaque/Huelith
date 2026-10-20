from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-sampling';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'workflow-settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png'
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',select=0,panel=6);wait()
before=wait()['recipe']
call(op='portrait_state',click='portraitSampleSkinColor');call(op='portrait_state',paint=[.5,.38]);state=wait()
settings=state['recipe']['people'][0]['settings'];assert len(settings['skinColor'])==3 and len(settings['skinSample'])==3
call(op='portrait_state',captureWidget='portraitSkinSamplePreview',capture=str(OUT/'source-thumbnail.png'))
picked=settings['skinColor'];sample=settings['skinSample'];size=Image.open(source).size
reference=call(op='portrait_geometry',source=str(source),sampleColor=[int(sample[0]*size[0]),int(sample[1]*size[1])])['color']
assert np.max(np.abs(np.array(picked)-reference))<1e-7
call(op='portrait_state',paint=[.6,.4]);assert wait()['recipe']['people'][0]['settings']['skinColor']==picked
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);state=wait()
saved=call(op='portrait_state',source=str(source),render=str(OUT/'sampled.png'),checkRegion=True,save=str(OUT/'sampling.huelith'))
assert saved['roundtrip'] and saved['regionError']<2e-6
flat=OUT/'flat.png';noise=OUT/'outlier.png';im=Image.new('RGB',(21,21),(180,115,95));im.save(flat);im.putpixel((10,10),(255,255,255));im.save(noise)
a=call(op='portrait_geometry',source=str(flat),sampleColor=[10,10])['color'];b=call(op='portrait_geometry',source=str(noise),sampleColor=[10,10])['color'];assert np.max(np.abs(np.array(a)-b))<1e-7
call(op='portrait_close');call(op='shutdown');print('Direct sampling, outlier rejection, automatic exit, undo and saved color passed.')
