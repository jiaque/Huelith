"""Bounded B01 check: measured surface, source-only reprojection, neutral and limits."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
os.environ['TEMP']=os.environ['TMP']=str(ROOT/'build/portrait-a01-temp')
os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe')
OUT=ROOT/'docs/test-evidence/M3/workspace-b01/orientation';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
try:
 for name in ['leroy-chiao','chris-hadfield','glasses']:
  source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';image=Image.open(source);size=list(image.size)
  people=call(op='portrait_probe',path=str(source),models=str(ROOT/'build/windows-release/models'))['people']
  if not people or len(people[0]['settings'].get('faceSurface',{}).get('points',[]))!=468:
   records.append({'name':name,'error':'surface missing'});continue
  recipe={'version':1,'people':people};person=people[0]
  person['settings']['protectBackground']=False
  (OUT/(name+'-recipe.json')).write_text(json.dumps(recipe),encoding='utf-8')
  x,y,w,h=person['face'];box=(int((x-w*.25)*size[0]),int((y-h*.3)*size[1]),int((x+w*1.25)*size[0]),int((y+h*1.3)*size[1]))
  sheet=Image.new('RGB',(5*240,2*330),(30,34,38));draw=ImageDraw.Draw(sheet)
  for row,key in enumerate(['head.yaw','head.pitch']):
   for column,value in enumerate([-100,-40,0,40,100]):
    person['settings']['parameters']={key:value};path=OUT/f'{name}-{key}-{value}.png'
    try:r=call(op='portrait_geometry',size=size,recipe=recipe,source=str(source),render=str(path),field=str(OUT/'field.bin'))
    except RuntimeError as error:
     records.append({'name':name,'key':key,'value':value,'rejected':str(error)})
     draw.text((column*240+5,row*330+5),f'{key} {value}: rejected',fill='white');continue
    assert r['identity']==(value==0)
    if value:
     mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
     assert np.isfinite(field).all()
     assert np.max(np.abs(field[0]))==0 and np.max(np.abs(field[-1]))==0
    crop=Image.open(path).crop(box);crop.thumbnail((240,300));sheet.paste(crop,(column*240,row*330+25));draw.text((column*240+5,row*330+5),f'{key} {value}',fill='white')
    records.append({'name':name,'key':key,'value':value,'milliseconds':r['milliseconds']})
  sheet.save(OUT/(name+'-comparison.png'))
 (OUT/'result.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps(records,ensure_ascii=True))
 assert not any('error' in r or 'rejected' in r for r in records),'Orientation acceptance failed; inspect result.json'
finally:call(op='shutdown')
