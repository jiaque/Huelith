from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-pelvis';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['leroy-chiao','christina-koch','jasmin-moghbeli','jessica-watkins','mediapipe-pose']:
 source=ROOT/'docs/test-evidence/M3/quality'/f'{name}-source.png'
 recipePath=ROOT/'docs/test-evidence/M3/semantic-shoulder'/f'{name}-production-recipe.json'
 if name=='mediapipe-pose':
  source=ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg';recipePath=source.with_name('mediapipe-pose-recipe.json')
 size=np.array(Image.open(source).size);recipe=json.loads(recipePath.read_text())
 for side in ['left','right']:
  recipe['people'][0]['settings']['parameters']={'body.hipWidth.'+side:100}
  try:
   r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'real-field.bin'),render=str(OUT/f'{name}-{side}.png'))
   w,h=r['meshSize'];f=np.fromfile(OUT/'real-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
   dy,dx=np.gradient(f,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min())
   records.append(dict(name=name,side=side,rendered=True,minimumJacobian=jac))
  except Exception as e:records.append(dict(name=name,side=side,rendered=False,reason=str(e)))
(OUT/'real-probe.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
call(op='shutdown');print(json.dumps(records,ensure_ascii=False,indent=2))
