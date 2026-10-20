"""Native detection/color fusion, with user decisions taking precedence."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,base64,io
OUT=ROOT/'docs/test-evidence/M3/blemish-model';source=ROOT/'docs/test-evidence/M3/quality/acne-face-source.png'
person=json.loads((source.parent/'acne-face-recipe.json').read_text())['people'][0]
evidence=OUT/'acne-face-native.png';labels=json.loads((ROOT/'docs/test-evidence/M3/semantic-blemish-categories/visual-regression-regions.json').read_text())
call(op='init',settings=str(OUT/'fusion-settings'))
def analyze(p=person,path=None):
    return call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True,**({'blemishEvidence':str(path)} if path else {}))['blemishes']
def near(marks,r):return [q for q in marks if np.hypot(q['x']-r['x'],q['y']-r['y'])<=r['radius']]
baseline=analyze();fused=analyze(path=evidence)
target=labels['targets'][0];assert not near(baseline,target)
hits=near(fused,target);assert len(hits)==1 and hits[0]['analysisVersion']==3 and hits[0]['state']=='pending',hits
independentTargets=[dict(id='small-cheek-red-spot',x=.49485,y=.44779,radius=.008),dict(id='small-lower-cheek-red-spot',x=.35725,y=.55022,radius=.008)]
for region in independentTargets:
    assert not near(baseline,region)
    found=near(fused,region)
    assert len(found)==1 and found[0]['analysisVersion']==4 and found[0]['category'] in ['red-texture','red-mark'] and found[0]['state']=='pending'
    # Visual spot targets do not assert texture; that is separately checked
    # against measured fine-detail evidence and paired controlled patches.
    if found[0]['category']=='red-texture':assert found[0]['interiorDetail']>=2 and found[0]['interiorDetail']>=1.5*found[0]['contextDetail']
    for state in ['preserved','repaired']:
        protected=copy.deepcopy(person);protected['settings']['blemishes']=[dict(found[0],state=state)]
        assert not near(analyze(protected,evidence),region)
for r in labels['protected']:assert not near(fused,r),(r,near(fused,r))
assert all(q in fused for q in baseline),'fusion removed existing accepted suggestions'
blank=OUT/'zero-evidence.png';Image.new('L',Image.open(source).size,0).save(blank);assert analyze(path=blank)==baseline
for state in ['preserved','repaired']:
    p=copy.deepcopy(person);p['settings']['blemishes']=[dict(hits[0],state=state)]
    assert not near(analyze(p,evidence),target),state
# A small protected island inside the proposed circle, off its center,
# must not be overwritten by model-supported expansion.
p=copy.deepcopy(person);mask=Image.new('L',Image.open(source).size,0);w,h=mask.size
cx,cy=int((target['x']+.02)*w),int(target['y']*h)
ImageDraw.Draw(mask).ellipse((cx-3,cy-3,cx+3,cy+3),fill=255)
buffer=io.BytesIO();mask.save(buffer,format='PNG');p['settings']['masks']={'repair.preserve':base64.b64encode(buffer.getvalue()).decode()}
assert not near(analyze(p,evidence),target),'off-center preserve ignored'
image=Image.open(source).convert('RGB');draw=ImageDraw.Draw(image)
for q in fused:
    x,y=q['x']*w,q['y']*h;r=q['radius']*min(w,h);draw.ellipse((x-r,y-r,x+r,y+r),outline='lime' if q['analysisVersion']==3 else 'orange',width=2)
image.save(OUT/'fusion-overlay.png')
(OUT/'fusion-results.json').write_text(json.dumps(dict(baselineCount=len(baseline),fusedCount=len(fused),recoveredTarget=hits[0],independentTargets=independentTargets,protectedStructuresClear=True,zeroEvidenceUnchanged=True,preservedAndRepairedExcluded=True,offCenterPreserveExcluded=True,candidates=fused),indent=2))
call(op='shutdown');print('Target recovered; nose/ear excluded; preserved/repaired decisions and off-center keep mask respected.')
