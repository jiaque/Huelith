from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-pelvis';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
size=np.array([640,900]);recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose-recipe.json').read_text())
p=recipe['people'][0];p['bodyAmbiguous']=False;p['settings']['parameters']={}
pose=[[.5,.5,1] for _ in range(33)]
for i,q in {11:(230,220),12:(410,220),13:(150,320),14:(490,320),15:(130,400),16:(510,400),23:(260,480),24:(380,480),25:(260,700),26:(380,700)}.items():pose[i]=[q[0]/639,q[1]/899,1]
p['settings']['pose']=pose
mask=Image.new('L',tuple(size));draw=ImageDraw.Draw(mask)
draw.polygon([(240,200),(400,200),(390,380),(425,480),(400,550),(390,780),(250,780),(240,550),(215,480),(250,380)],fill=255)
buffer=io.BytesIO();mask.save(buffer,format='PNG');p['bodyMask']=base64.b64encode(buffer.getvalue()).decode()
records=[]
for amount in [-100,100]:
 p['settings']['parameters']={'body.hipWidth.left':amount,'body.hipWidth.right':amount}
 result=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'))
 w,h=result['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2]
 points=np.array([[215,480],[425,480],[320,480],[250,380],[390,380],[260,700],[380,700]],float)/(size-1)
 motion=(forward_points(f,points)-points)*(size-1)
 assert motion[0,0]*amount<0 and motion[1,0]*amount>0,motion
 assert np.linalg.norm(motion[:2],axis=1).min()>8
 assert np.abs(motion[2:]).max()<.1,motion
 dy,dx=np.gradient(f,1/(h-1),1/(w-1),axis=(0,1));jac=float(((1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]).min());assert jac>.1
 records.append(dict(amount=amount,motion=motion.tolist(),minimumJacobian=jac))
(OUT/'controlled.json').write_text(json.dumps(records,indent=2))
call(op='shutdown');print('Pelvis width direction, central/waist/knee anchors and positive Jacobian passed.')
