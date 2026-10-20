"""Forehead tissue flow without eyebrow lifting or hairline enlargement."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-perioral-lift';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
real=json.loads((ROOT/'docs/test-evidence/M3/semantic-forehead-volume/real-recipe.json').read_text(encoding='utf-8'));source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';size=Image.open(source).size
recipe=copy.deepcopy(real);p=recipe['people'][0];p['settings']['parameters']={'face.perioralLift.left':100};lm=np.array(p['landmarks'])[:,:2]
r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),field=str(OUT/'field.bin'),render=str(OUT/'lift.png'));assert not r['identity']
w,h=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2];fixed=np.linalg.norm((forward_points(field,lm)-lm)*size,axis=1);assert fixed.max()<.15,float(fixed.max())
axis=(lm[42:48].mean(0)-lm[36:42].mean(0))*size;axis/=np.linalg.norm(axis);down=np.array([-axis[1],axis[0]])
motion=np.linalg.norm(field*size,axis=2);assert motion.max()>.2,float(motion.max());index=np.unravel_index(motion.argmax(),motion.shape);point=np.array([[index[1]/(w-1),index[0]/(h-1)]]);delta=(forward_points(field,point)-point)*size;assert float(delta[0]@down)<-.1
dx=np.gradient(field,1/(w-1),axis=1);dy=np.gradient(field,1/(h-1),axis=0);jac=(1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1];assert jac.min()>.35,float(jac.min())
skin=np.array(Image.open(io.BytesIO(base64.b64decode(p['settings']['automaticMasks']['faceSkin']))).convert('L').resize((w,h)))
assert motion[skin<20].max()<.15,float(motion[skin<20].max())
blocked=copy.deepcopy(recipe);blocked['people'][0]['settings']['automaticMasks'].pop('faceSkin');assert call(op='portrait_geometry',recipe=blocked,size=size)['identity']
frame=Image.new('L',(512,512),255);buffer=io.BytesIO();frame.save(buffer,format='PNG');blocked=copy.deepcopy(recipe);blocked['people'][0]['settings']['automaticMasks']['eyeglassFrame']=base64.b64encode(buffer.getvalue()).decode();assert call(op='portrait_geometry',recipe=blocked,size=size)['identity']
# Sample between eyebrow landmarks too; preserving just the five vertices
# would still allow a visible brow bend between anchors.
brow_points=np.array([lm[i]*(1-t)+lm[i+1]*t for i in list(range(17,21))+list(range(22,26)) for t in np.linspace(0,1,10)])
assert np.linalg.norm(sample(field,brow_points)*size,axis=1).max()<.15
lip=lm[[58,48,49,50,51,52,62,53,54,55,56,57]];mouth=np.array([lip.mean(0)+(vertex-lip.mean(0))*t for vertex in lip for t in np.linspace(0,1,10)]);assert np.linalg.norm(sample(field,mouth)*size,axis=1).max()<.15

combined=copy.deepcopy(recipe);combined['people'][0]['settings']['parameters'].update({'face.contourLift.left':100,'face.contourLift.right':100,'face.malarShape.left':100,'face.malarShape.right':100})
rr=call(op='portrait_geometry',recipe=combined,size=size,source=str(source),field=str(OUT/'combined-field.bin'),render=str(OUT/'combined.png'));cw,ch=rr['meshSize'];cf=np.fromfile(OUT/'combined-field.bin',np.float32).reshape(ch,cw,4)[:,:,:2];cx=np.gradient(cf,1/(cw-1),axis=1);cy=np.gradient(cf,1/(ch-1),axis=0);cj=(1+cx[:,:,0])*(1+cy[:,:,1])-cy[:,:,0]*cx[:,:,1];assert cj.min()>.25,float(cj.min())
extra=[]
for name in ['chris-hadfield','john-glenn']:
 other=json.loads((ROOT/f'docs/test-evidence/M3/semantic-tear-volume/{name}-recipe.json').read_text(encoding='utf-8'));photo=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';photo_size=Image.open(photo).size
 for person in other['people']:person['settings']['parameters']={}
 person=max(other['people'],key=lambda x:x['face'][2]*x['face'][3]);person['settings']['parameters']={'face.perioralLift.left':100,'face.perioralLift.right':100};rr=call(op='portrait_geometry',recipe=other,size=photo_size,source=str(photo),field=str(OUT/'quality.bin'),render=str(OUT/f'{name}.png'));assert not rr['identity'],name
 qw,qh=rr['meshSize'];qf=np.fromfile(OUT/'quality.bin',np.float32).reshape(qh,qw,4)[:,:,:2];points=np.array(person['landmarks'])[:,:2];drift=np.linalg.norm((forward_points(qf,points)-points)*photo_size,axis=1).max();assert drift<.15,(name,float(drift));qx=np.gradient(qf,1/(qw-1),axis=1);qy=np.gradient(qf,1/(qh-1),axis=0);qj=(1+qx[:,:,0])*(1+qy[:,:,1])-qy[:,:,0]*qx[:,:,1];assert qj.min()>.35,(name,float(qj.min()));extra.append(dict(photo=name,landmarkDrift=float(drift),minimumJacobian=float(qj.min())))
(OUT/'additional-portraits.json').write_text(json.dumps(extra,indent=2),encoding='utf-8')
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real['people'][0]['settings']['parameters']={};call(op='portrait_window',path=str(source),recipe=real);wait();call(op='portrait_state',panel=0,select=0,linked=False,side=0);before=wait()['recipe'];call(op='portrait_state',parameter='face.perioralLift',value=80);changed=wait()['recipe'];assert changed['people'][0]['settings']['parameters']['face.perioralLift.left']==80
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'real.png'),save=str(OUT/'perioral-lift.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=changed);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'real.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');(OUT/'results.json').write_text(json.dumps(dict(maxFixedDrift=float(fixed.max()),maxMotion=float(motion.max()),minimumJacobian=float(jac.min()),outsideSkinDrift=float(motion[skin<20].max())),indent=2),encoding='utf-8');print('Perioral upward motion, brow/eye/hair protection, nonfolding, missing-skin gate, undo, ROI and persistence passed')
