from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-repair-segments';call(op='init',settings=str(OUT/'workflow-settings'))
source=ROOT/'docs/test-evidence/M3/quality/john-glenn-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/john-glenn-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait();call(op='portrait_state',click='portraitRepairBrush',brushSize=22);before=wait()['recipe']
# Follow the visible horizontal forehead crease in the original image.
path=[[x/943,(249-4*(x-335)/70)/1200] for x in range(335,406,5)]
start=time.monotonic();call(op='portrait_state',paintPath=path);print('Gesture dispatch seconds',time.monotonic()-start);state=wait();repairs=state['recipe']['people'][0]['settings']['repairs'];assert len(repairs)==1 and len(repairs[0]['sourceOffsets'])==len(repairs[0]['points']) and len(repairs[0]['points'])>3
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);wait();saved=call(op='portrait_state',source=str(source),render=str(OUT/'john-glenn-corrected.png'),checkRegion=True,save=str(OUT/'wrinkle.huelith'));assert saved['roundtrip'] and saved['regionError']<2e-6,saved
# Return to the original stroke to check exact reopening.

recipe=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',source=str(source),render=str(OUT/'john-glenn-reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'john-glenn-corrected.png')),np.asarray(Image.open(OUT/'john-glenn-reopened.png')))
size=Image.open(source).size;box=(290,210,440,280);sheet=Image.new('RGB',(600,140));sheet.paste(Image.open(source).crop(box).resize((300,140)),(0,0));sheet.paste(Image.open(OUT/'john-glenn-corrected.png').crop(box).resize((300,140)),(300,0));sheet.save(OUT/'comparison.png')
assert np.max(abs(np.asarray(Image.open(source).convert('RGB'),dtype=int)-np.asarray(Image.open(OUT/'john-glenn-corrected.png').convert('RGB'),dtype=int)))>0
call(op='portrait_close');call(op='shutdown');print('Continuous wrinkle stroke, one undo transaction, ROI, save and reopen passed.')
