from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-wrinkle';call(op='init',settings=str(OUT/'workflow-settings'))
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
call(op='portrait_state',paintPath=path,paintEnd='ungrab');state=wait();repairs=state['recipe']['people'][0]['settings']['repairs'];assert len(repairs)==1 and len(repairs[0]['sourceOffsets'])==len(repairs[0]['points']) and len(repairs[0]['points'])>3

call(op='portrait_close');call(op='shutdown');print('Interrupted stroke committed exactly once.')
