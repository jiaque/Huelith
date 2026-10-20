"""Fast and settled previews must reuse the native-coordinate target field."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],str(Path('tools/portrait_freeze_workflow_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/preview-geometry';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'));results=[]
for case in ['frozen','large']:
 if case=='frozen':call(op='portrait_window',project=str(ROOT/'docs/test-evidence/M3/liquify-freeze/freeze.huelith'))
 else:
  source=ROOT/'research/m3/portraits/jassi-standing.jpg';recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text())
  for p in recipe['people']:p['settings']['parameters']={}
  call(op='portrait_window',path=str(source),recipe=recipe)
 size=np.array(Image.open(source).size);wait()
 call(op='portrait_state',select=0,panel=4,brushSize=100,liquifyTool=1 if case=='frozen' else 0);before=wait()
 fx,fy,fw,fh=before['recipe']['people'][0]['face'];start=np.array([fx+fw*.45,fy+fh*.65]);end=start+[fw*.03,0]
 record=dict(case=case,sourceSize=size.tolist())
 for stroke in range(2):
  path=[(start+(end-start)*t).tolist() for t in np.linspace(0,1,12)]
  previous=wait();call(op='portrait_state',paintPath=path,paintStepMs=35,paintEnd='hold');held=wait()
  r=call(op='portrait_state',previewField=str(OUT/f'{case}-{stroke}-fast.bin'),previewImage=str(OUT/f'{case}-{stroke}-fast.png'))
  assert all(r['previewMeshSize'])
  call(op='portrait_geometry',size=size.tolist(),recipe=held['recipe'],field=str(OUT/'native.bin'))
  assert (OUT/'native.bin').read_bytes()==(OUT/f'{case}-{stroke}-fast.bin').read_bytes(),'fast target differs from original-coordinate render'
  call(op='portrait_state',paintRelease=end.tolist());settled=wait()
  call(op='portrait_state',previewField=str(OUT/f'{case}-{stroke}-fine.bin'),previewImage=str(OUT/f'{case}-{stroke}-fine.png'))
  assert (OUT/f'{case}-{stroke}-fast.bin').read_bytes()==(OUT/f'{case}-{stroke}-fine.bin').read_bytes(),'release changed target geometry'
  assert settled['previewGeometryPasses']==held['previewGeometryPasses'],'settling unnecessarily recomputed the same geometry'
  color_passes=settled['previewColorPasses']-previous['previewColorPasses']
  assert color_passes<=(1 if stroke==0 else 0),color_passes
  record['stroke'+str(stroke)]=dict(colorPasses=color_passes,geometryPasses=held['previewGeometryPasses']-previous['previewGeometryPasses'],settledGeometryPasses=settled['previewGeometryPasses']-held['previewGeometryPasses'])
 results.append(record);call(op='portrait_close')
(OUT/'results.json').write_text(json.dumps(results,indent=2));call(op='shutdown');print(results)
