"""Real-image detection/fusion review; not a precision/recall acceptance test."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
from collections import Counter
OUT=ROOT/'docs/test-evidence/M3/blemish-model/real-review';OUT.mkdir(parents=True,exist_ok=True)
model=ROOT/'research/m3/blemish-model-evaluation/blemish_detection_modelscope.onnx'
call(op='init',settings=str(OUT/'settings'))
records=[]
for name in ['acne-oily','acne-face','jessica-watkins','john-glenn','bernard-harris','mae-jemison']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png'
    person=json.loads(source.with_name(name+'-recipe.json').read_text())['people'][0]
    mask=OUT/(name+'-evidence.png')
    call(op='portrait_geometry',source=str(source),person=person,blemishModel=str(model),mask=str(mask))
    baseline=call(op='portrait_geometry',source=str(source),person=person,repairAnalysis=True)['blemishes']
    fused=call(op='portrait_geometry',source=str(source),person=person,repairAnalysis=True,blemishEvidence=str(mask))['blemishes']
    im=Image.open(source).convert('RGB');draw=ImageDraw.Draw(im)
    for q in fused:
        x,y=q['x']*im.width,q['y']*im.height;r=q['radius']*min(im.size)
        draw.ellipse((x-r,y-r,x+r,y+r),outline='lime' if q['analysisVersion']==3 else 'orange',width=2)
    x,y,w,h=person['face'];im=im.crop((max(0,int((x-.2*w)*im.width)),max(0,int((y-.2*h)*im.height)),min(im.width,int((x+1.2*w)*im.width)),min(im.height,int((y+1.2*h)*im.height))))
    im.thumbnail((600,600));im.save(OUT/(name+'-review.png'))
    records.append(dict(name=name,baselineCount=len(baseline),fusedCount=len(fused),categories=dict(Counter(q['category'] for q in fused)),candidates=fused))
(OUT/'results.json').write_text(json.dumps(dict(acceptance=False,reason='Unannotated review images; counts are not accuracy',cases=records),indent=2))
call(op='shutdown');print([(r['name'],r['baselineCount'],r['fusedCount']) for r in records])
