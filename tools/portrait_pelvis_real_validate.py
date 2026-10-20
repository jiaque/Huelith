from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-pelvis';call(op='init',settings=str(OUT/'settings'));records=[]
for name,index in [('FudanPed00001',1),('FudanPed00003',0)]:
 source=ROOT/'research/m2/instances'/f'{name}.png';size=np.array(Image.open(source).size)
 recipe=json.loads((OUT/f'{name}-recipe.json').read_text());p=recipe['people'][index]
 pose=np.array(p['settings']['pose'])[:,:2]*(size-1);origin=pose[[23,24]].mean(0)
 axis=origin-pose[[11,12]].mean(0);length=np.linalg.norm(axis);axis/=length;normal=np.array([-axis[1],axis[0]])
 normal*=1 if np.dot(pose[23]-origin,normal)>0 else -1
 mask=np.array(Image.open(io.BytesIO(base64.b64decode(p['bodyMask']))));mh,mw=mask.shape
 radii=np.arange(.5,np.linalg.norm(pose[23]-pose[24])*1.4,.5)
 points=origin+normal*radii[:,None];pixels=(points/(size-1)*[mw,mh]).astype(int)
 valid=mask[np.clip(pixels[:,1],0,mh-1),np.clip(pixels[:,0],0,mw-1)]>180
 radius=radii[np.flatnonzero(~valid)[0]]-.25;target=origin+normal*radius
 probes=np.vstack([target,origin,origin-axis*length*.35,pose[25],pose[26],pose[11],pose[12]])/(size-1)
 for amount in [-100,100]:
  p['settings']['parameters']={'body.hipWidth.left':amount}
  r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'real-field.bin'),render=str(OUT/f'{name}-left-{amount}.png'))
  w,h=r['meshSize'];f=np.fromfile(OUT/'real-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  motion=(forward_points(f,probes)-probes)*(size-1);shift=float(np.dot(motion[0],normal))
  assert shift*amount>0 and abs(shift)>.5,motion
  assert np.linalg.norm(motion[1:],axis=1).max()<.2,motion
  otherDrift=0.
  for otherIndex,other in enumerate(recipe['people']):
   if otherIndex==index:continue
   otherMask=np.array(Image.open(io.BytesIO(base64.b64decode(other['bodyMask']))))
   yy,xx=np.where(otherMask>200);protected=np.stack([xx/(otherMask.shape[1]-1),yy/(otherMask.shape[0]-1)],axis=1)[::4]
   if len(protected):otherDrift=max(otherDrift,float(np.linalg.norm((forward_points(f,protected)-protected)*(size-1),axis=1).max()))
  assert otherDrift<.5,otherDrift
  dy,dx=np.gradient(f,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min());assert jac>.1
  records.append(dict(name=name,amount=amount,contourShift=shift,fixedMaximum=float(np.linalg.norm(motion[1:],axis=1).max()),otherPersonMaximum=otherDrift,minimumJacobian=jac))
(OUT/'real-validation.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(json.dumps(records,indent=2))
