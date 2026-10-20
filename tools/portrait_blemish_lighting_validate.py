"""Track a real missed spot and a normal contour rejected by visual review."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/blemish-model'
call(op='init',settings=str(OUT/'lighting-settings'))
cases=[('acne-oily','target',.37636,.15181,.02),('john-glenn','protected-contour',.48663,.31885,.006)]
records=[]
for name,kind,x,y,radius in cases:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png'
    person=json.loads(source.with_name(name+'-recipe.json').read_text())['people'][0]
    result=call(op='portrait_geometry',source=str(source),person=person,repairAnalysis=True,blemishEvidence=str(OUT/'real-review'/(name+'-evidence.png')))
    hits=[q for q in result['blemishes'] if np.hypot(q['x']-x,q['y']-y)<=radius]
    if kind=='protected-contour':assert not hits,'smooth face contour falsely suggested'
    records.append(dict(image=name,kind=kind,x=x,y=y,radius=radius,hits=hits))
complete=all(bool(r['hits'])==(r['kind']=='target') for r in records)
(OUT/'lighting-regions-results.json').write_text(json.dumps(dict(complete=complete,annotation='Assistant visual appearance review, not diagnosis or user preference; unmarked pixels unknown',cases=records),indent=2))
call(op='shutdown');print('Contour protection passed; complete including target recall:',complete)
