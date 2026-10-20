from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import base64,io,copy
OUT=ROOT/'docs/test-evidence/M3/manual-scale'
source=ROOT/'research/m2/instances/FudanPed00001.png';size=np.array(Image.open(source).size)
base=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/FudanPed00001-recipe.json').read_text())
for p in base['people']:p['settings']['parameters']={}
call(op='init',settings=str(OUT/'neighbor-settings'))
call(op='portrait_geometry',size=size.tolist(),recipe=base,source=str(source),render=str(OUT/'neighbors-before.png'))
a=np.array(Image.open(OUT/'neighbors-before.png'))
mask=np.array(Image.open(io.BytesIO(base64.b64decode(base['people'][0]['bodyMask']))).resize(tuple(size),Image.Resampling.NEAREST))>200
records=[]
for amount in [-100,100]:
 recipe=copy.deepcopy(base);p=recipe['people'][1];x,y,w,h=p['body']
 p['settings']['manualScaleRegion']=[x+w*.5,y+h*.6,.08,.18]
 p['settings']['parameters']={'body.manualScale':amount}
 result=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,source=str(source),field=str(OUT/'neighbor-field.bin'),render=str(OUT/f'neighbors-{amount}.png'))
 b=np.array(Image.open(OUT/f'neighbors-{amount}.png'))
 assert np.array_equal(a[mask],b[mask]),'manual scale changed adjacent person'
 mw,mh=result['meshSize'];f=np.fromfile(OUT/'neighbor-field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
 magnitude=float(np.linalg.norm(f*(size-1),axis=2).max());assert magnitude>2
 records.append(dict(amount=amount,neighborPixelDifference=0,maximumDisplacement=magnitude))
(OUT/'neighbors.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(records)
