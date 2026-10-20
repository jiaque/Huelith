from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import base64,io
OUT=ROOT/'docs/test-evidence/M3/manual-height';source=ROOT/'research/m3/portraits/jassi-standing.jpg';size=np.array(Image.open(source).size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text());p=recipe['people'][0]
hair=np.array(Image.open(io.BytesIO(base64.b64decode(p['settings']['automaticMasks']['hair']))));yy,xx=np.where(hair>200)
hairPoints=np.stack([xx/(hair.shape[1]-1),yy/(hair.shape[0]-1)],axis=1)
face=np.array(p['landmarks'])[:,:2];points=np.concatenate([hairPoints,face])
call(op='init',settings=str(OUT/'settings'));records=[]
for angle in [0,.06]:
 p['settings']['manualHeightRange']=[[.48,.55],[.48+angle,.84]]
 delta=np.array([angle,.29])*(size-1)
 for amount in [-100,100]:
  p['settings']['parameters']={'body.manualHeight':amount}
  r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'surface-field.bin'),source=str(source),render=str(OUT/f'jassi-standing-{angle}-{amount}.png'))
  w,h=r['meshSize'];f=np.fromfile(OUT/'surface-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  motion=(forward_points(f,points)-points)*(size-1);error=float(np.linalg.norm(motion+delta*.12*(amount/100),axis=1).max())
  dy,dx=np.gradient(f,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min())
  records.append(dict(angle=angle,amount=amount,headShapeError=error,minimumJacobian=jac))
  assert error<.5,records[-1]
  assert jac>.1,records[-1]
(OUT/'surface.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(json.dumps(records,indent=2))
