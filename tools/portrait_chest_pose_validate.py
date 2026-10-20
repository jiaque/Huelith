from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import base64,io,copy
OUT=ROOT/'docs/test-evidence/M3/semantic-chest';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
source=ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg';size=np.array(Image.open(source).size)
recipe=json.loads((source.parent/'mediapipe-pose-recipe.json').read_text());p=recipe['people'][0]
for label,path in [('open-arms-left',[[.456,.522],[.461,.545],[.461,.57]]),('open-arms-right',[[.538,.522],[.536,.545],[.534,.57]])]:
 p['settings']['chestContourPath']=path
 for amount in [-100,100]:
  p['settings']['parameters']={'body.chest':amount}
  result=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,source=str(source),field=str(OUT/'manual-field.bin'),render=str(OUT/f'{label}-{amount}.png'))
  w,h=result['meshSize'];field=np.fromfile(OUT/'manual-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
  points=np.array(path);motion=(forward_points(field,points)-points)*(size-1)
  print(amount,motion.tolist(),flush=True)
  assert np.linalg.norm(motion[1])>1 and np.linalg.norm(motion[[0,2]],axis=1).max()<.5
  pose=np.array(p['settings']['pose'])[:,:2]*(size-1);span=np.linalg.norm(pose[11]-pose[12]);arm=[]
  for side in [0,1]:
   for segment in [0,1]:
    a,b=pose[11+side+segment*2],pose[13+side+segment*2];v=b-a;normal=np.array([-v[1],v[0]])/np.linalg.norm(v)
    arm.extend([a+v*t+normal*span*n for t in np.linspace(0,1,31) for n in [-.08,0,.08]])
  arm=np.array(arm)/(size-1);armDrift=float(np.linalg.norm((forward_points(field,arm)-arm)*(size-1),axis=1).max())
  dy,dx=np.gradient(field,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min())
  print('arm',armDrift,'minimumJacobian',jac,flush=True)
  assert armDrift<.5 and jac>.1
call(op="shutdown")
