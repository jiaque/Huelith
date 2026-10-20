from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-pelvis';source=ROOT/'research/m3/portraits/jassi-standing.jpg'
size=np.array(Image.open(source).size);recipe=json.loads((OUT/'jassi-standing-recipe.json').read_text());p=recipe['people'][0]
call(op='init',settings=str(OUT/'settings'));records=[]
for side in ['left','right','both']:
 for amount in [-100,100]:
  p['settings']['parameters']={f'body.hipWidth.{s}':amount for s in (['left','right'] if side=='both' else [side])}
  r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'standing-field.bin'),render=str(OUT/f'jassi-standing-{side}-{amount}.png'))
  w,h=r['meshSize'];f=np.fromfile(OUT/'standing-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  # Broad source regions cover head, torso, arms, hands, and distal legs.
  xx,yy=np.meshgrid(np.linspace(.08,.75,65),np.linspace(.1,.94,100));grid=np.stack([xx.ravel(),yy.ravel()],axis=1)
  protected=grid[(grid[:,1]<.463)|(grid[:,1]>.57)]
  drift=float(np.linalg.norm((forward_points(f,protected)-protected)*(size-1),axis=1).max())
  assert drift<.5,(side,amount,drift)
  dy,dx=np.gradient(f,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min());assert jac>.1
  magnitude=float(np.linalg.norm(f*(size-1),axis=2).max());assert magnitude>5
  records.append(dict(side=side,amount=amount,protectedMaximum=drift,minimumJacobian=jac,maximumDisplacement=magnitude))
(OUT/'standing-validation.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(json.dumps(records,indent=2))
