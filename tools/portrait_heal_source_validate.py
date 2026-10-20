from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-heal-source';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=OUT/'uniform.png';Image.new('RGB',(256,256),(180,140,110)).save(source)
mask=Image.new('L',(256,256),0);ImageDraw.Draw(mask).rectangle((155,100,205,156),fill=255);mask.save(OUT/'allowed.png')
stroke=dict(version=1,points=[[.5,.5]],radius=.03,feather=.5,opacity=1)
r=call(op='portrait_geometry',source=str(source),healingOffset=stroke,sourceMask=str(OUT/'allowed.png'));q=(np.array([.5,.5])+r['offset'])*256;assert 163<q[0]<197 and 108<q[1]<148,q
Image.new('L',(256,256),0).save(OUT/'empty.png')
try:call(op='portrait_geometry',source=str(source),healingOffset=stroke,sourceMask=str(OUT/'empty.png'));raise AssertionError('Empty source must fail')
except RuntimeError as e:assert 'Alt' in str(e)
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait();call(op='portrait_state',click='portraitRepairBrush',brushSize=8)
x,y,w,h=recipe['people'][0]['face'];before=wait()['recipe'];call(op='portrait_state',paint=[x+w*.25,y+h*.65]);r=wait();assert len(r['recipe']['people'][0]['settings'].get('repairs',[]))>0,r['status']
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);wait();saved=call(op='portrait_state',source=str(source),render=str(OUT/'repair.png'),checkRegion=True,save=str(OUT/'repair.huelith'));assert saved['roundtrip'] and saved['regionError']<2e-6
call(op='portrait_close');call(op='shutdown');print('Constrained source footprint, empty-source failure, actual click repair and saved workflow passed.')
