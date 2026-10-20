"""Measure reviewed regions without claiming unreviewed pixels are negatives."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-blemish-categories'
labels=json.loads((OUT/'visual-regression-regions.json').read_text())
source=ROOT/labels['source'];recipe=json.loads((source.parent/'acne-face-recipe.json').read_text())
call(op='init',settings=str(OUT/'visual-regions-settings'))
r=call(op='portrait_geometry',source=str(source),person=recipe['people'][0],repairAnalysis=True)
call(op='shutdown')
def hits(region):
    return [q for q in r['blemishes'] if np.hypot(q['x']-region['x'],q['y']-region['y'])<=region['radius']]
report=dict(targets=[dict(id=q['id'],detected=bool(hits(q))) for q in labels['targets']],protected=[dict(id=q['id'],falseCandidates=len(hits(q))) for q in labels['protected']],allCandidates=len(r['blemishes']))
report['complete']=all(q['detected'] for q in report['targets']) and all(q['falseCandidates']==0 for q in report['protected'])
(OUT/'visual-regions-results.json').write_text(json.dumps(report,indent=2));print(report)
assert all(q['falseCandidates']==0 for q in report['protected']),'known anatomical false-positive regression'
# Missing positive targets stay explicitly incomplete; this is not an overall acceptance gate.
