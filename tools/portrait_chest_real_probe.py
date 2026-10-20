from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import base64,io,copy
OUT=ROOT/'docs/test-evidence/M3/semantic-chest';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['leroy-chiao','christina-koch','jasmin-moghbeli']:
 source=ROOT/'docs/test-evidence/M3/quality'/f'{name}-source.png';size=np.array(Image.open(source).size)
 recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-shoulder'/f'{name}-production-recipe.json').read_text());p=recipe['people'][0];pose=np.array(p['settings']['pose'])[:,:2]*(size-1)
 origin=pose[[11,12]].mean(0);delta=pose[[23,24]].mean(0)-origin;length=np.linalg.norm(delta);axis=delta/length;normal=np.array([-axis[1],axis[0]])
 body=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['bodyMask']))).convert('L'))
 def inside(q):
  x,y=(q/(size-1)*np.array(body.shape[::-1])).astype(int)
  return 0<=x<body.shape[1] and 0<=y<body.shape[0] and body[y,x]>180
 for side in [-1,1]:
  for t in [.22,.32,.4]:
   center=origin+axis*length*t;radius=0
   for r in np.arange(1,length,1):
    if not inside(center+normal*side*r):radius=r-.5;break
   target=center+normal*side*radius;p['settings']['chestContour']=(target/(size-1)).tolist();p['settings']['parameters']={'body.chest':100}
   label=f'{name}-{side}-{t}'
   try:result=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,source=str(source),field=str(OUT/'field.bin'),render=str(OUT/(label+'.png')))
   except RuntimeError as e:records.append(dict(name=name,side=side,t=t,refused=str(e)));continue
   w,h=result['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
   shift=(forward_points(field,np.array([target/(size-1)]))[0]*(size-1)-target)
   fixed=np.concatenate([pose[[11,12,23,24]],np.array(p['landmarks'])[:,:2]*(size-1)])/ (size-1)
   drift=float(np.linalg.norm((forward_points(field,fixed)-fixed)*(size-1),axis=1).max())
   record=dict(name=name,side=side,t=t,target=target.tolist(),outward=float(np.dot(shift,normal)*side),fixedDrift=drift);records.append(record);print(record,flush=True)
   assert record['outward']>1 and drift<.5
   (OUT/(label+'-recipe.json')).write_text(json.dumps(recipe))
(OUT/'real-probe.json').write_text(json.dumps(records,ensure_ascii=False,indent=2));print('successful cases',sum('outward' in r for r in records),'of',len(records));call(op='shutdown')
