"""Forehead tissue flow without eyebrow lifting or hairline enlargement."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-forehead-lift';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
real=json.loads((ROOT/'docs/test-evidence/M3/semantic-forehead-volume/real-recipe.json').read_text(encoding='utf-8'));source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';size=Image.open(source).size
recipe=copy.deepcopy(real);p=recipe['people'][0];p['settings']['parameters']={'face.foreheadLift':100};lm=np.array(p['landmarks'])[:,:2]
r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),field=str(OUT/'field.bin'),render=str(OUT/'lift.png'));assert not r['identity']
w,h=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2];fixed=np.linalg.norm((forward_points(field,lm)-lm)*size,axis=1);assert fixed.max()<.15,float(fixed.max())
axis=(lm[42:48].mean(0)-lm[36:42].mean(0))*size;axis/=np.linalg.norm(axis);down=np.array([-axis[1],axis[0]])
motion=np.linalg.norm(field*size,axis=2);assert motion.max()>1,float(motion.max());index=np.unravel_index(motion.argmax(),motion.shape);point=np.array([[index[1]/(w-1),index[0]/(h-1)]]);delta=(forward_points(field,point)-point)*size;assert float(delta[0]@down)<-1
dx=np.gradient(field,1/(w-1),axis=1);dy=np.gradient(field,1/(h-1),axis=0);jac=(1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1];assert jac.min()>.35,float(jac.min())
skin=np.array(Image.open(io.BytesIO(base64.b64decode(p['settings']['automaticMasks']['faceSkin']))).convert('L').resize((w,h)))
assert motion[skin<20].max()<.15,float(motion[skin<20].max())
blocked=copy.deepcopy(recipe);blocked['people'][0]['settings']['automaticMasks'].pop('faceSkin');assert call(op='portrait_geometry',recipe=blocked,size=size)['identity']
combined=copy.deepcopy(recipe);combined['people'][0]['settings']['parameters'].update({'face.contourLift.left':100,'face.contourLift.right':100,'face.malarShape.left':100,'face.malarShape.right':100})
rr=call(op='portrait_geometry',recipe=combined,size=size,source=str(source),field=str(OUT/'combined-field.bin'),render=str(OUT/'combined.png'));cw,ch=rr['meshSize'];cf=np.fromfile(OUT/'combined-field.bin',np.float32).reshape(ch,cw,4)[:,:,:2];cx=np.gradient(cf,1/(cw-1),axis=1);cy=np.gradient(cf,1/(ch-1),axis=0);cj=(1+cx[:,:,0])*(1+cy[:,:,1])-cy[:,:,0]*cx[:,:,1];assert cj.min()>.25,float(cj.min())
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real['people'][0]['settings']['parameters']={};call(op='portrait_window',path=str(source),recipe=real);wait();call(op='portrait_state',panel=0,select=0);before=wait()['recipe'];call(op='portrait_state',parameter='face.foreheadLift',value=80);changed=wait()['recipe'];assert changed['people'][0]['settings']['parameters']['face.foreheadLift']==80
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'real.png'),save=str(OUT/'forehead-lift.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=changed);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'real.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');(OUT/'results.json').write_text(json.dumps(dict(maxFixedDrift=float(fixed.max()),maxMotion=float(motion.max()),minimumJacobian=float(jac.min()),outsideSkinDrift=float(motion[skin<20].max())),indent=2),encoding='utf-8');print('Forehead upward motion, brow/eye/hair protection, nonfolding, missing-skin gate, undo, ROI and persistence passed')
