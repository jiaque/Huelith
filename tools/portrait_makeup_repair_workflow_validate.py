from pathlib import Path
import cv2, copy
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-makeup-repair';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={};p['settings']['mouthTopologyVersion']=2
w,h=Image.open(source).size
records=json.loads((OUT/'cases.json').read_text())
case=records[1];target=case['target']
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait();call(op='portrait_state',click='portraitMakeupRepair');before=wait()['recipe']
call(op='portrait_state',paint=target);state=wait();strokes=state['recipe']['people'][0]['settings'].get('repairs',[]);assert len(strokes)==1,(state['status'],strokes);assert strokes[0]['makeupPart']=='lip.lower'
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'makeup.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
# Moving a selected source to the forehead must be rejected without changing the recipe.
call(op='portrait_state',repairSelect=0);valid=wait()['recipe'];call(op='portrait_state',paint=[.5,.2],alt=True);assert wait()['recipe']==valid
call(op='portrait_close')
# Controlled flaky pigment defect on a real lip. The expected original is known.
original=np.array(Image.open(source).convert('RGB'));damaged=original.copy();cx,cy=int(target[0]*w),int(target[1]*h);damaged[cy-1:cy+2,cx-1:cx+2]=np.clip(damaged[cy-1:cy+2,cx-1:cx+2].astype(float)+55,0,255).astype(np.uint8);damagedfile=OUT/'controlled-flake.png';Image.fromarray(damaged).save(damagedfile)
stroke=copy.deepcopy(case['stroke']);stroke.pop('makeupPart');stroke['radius']=max(case['radius'],3)/min(w,h)
matched=call(op='portrait_geometry',source=str(damagedfile),person=p,makeupRepair=stroke)['stroke'];matched.update(kind='makeup',id='test');p['settings']['repairs']=[matched]
call(op='portrait_geometry',recipe=recipe,size=[w,h],source=str(damagedfile),render=str(OUT/'repaired-flake.png'))
repaired=np.array(Image.open(OUT/'repaired-flake.png').convert('RGB'));beforeError=np.mean(abs(damaged[cy-1:cy+2,cx-1:cx+2].astype(float)-original[cy-1:cy+2,cx-1:cx+2]));afterError=np.mean(abs(repaired[cy-1:cy+2,cx-1:cx+2].astype(float)-original[cy-1:cy+2,cx-1:cx+2]));assert afterError<beforeError*.35,(beforeError,afterError)
box=(cx-45,cy-22,cx+45,cy+23);sheet=Image.new('RGB',(540,90));
for i,img in enumerate([original,damaged,repaired]):sheet.paste(Image.fromarray(img).crop(box).resize((180,90)),(180*i,0))
sheet.save(OUT/'flake-comparison.png')
call(op='shutdown');print('Direct default brush, same-part source, undo/save/ROI; controlled defect error',beforeError,afterError)
