"""D14 production crossed-arm case: usable side, fixed neck/head, continuous field."""
from pathlib import Path
exec(compile(Path('tools/portrait_shoulder_width_surface_validate.py').read_text(encoding='utf-8').split("\nOUT=ROOT/'docs/test-evidence/M3/semantic-shoulder';call")[0], 'tools/portrait_shoulder_width_surface_validate.py','exec'))
import base64,io
OUT=ROOT/'docs/test-evidence/M3/semantic-shoulder'
call(op='init',settings=str(OUT/'single-surface-settings'))
source=ROOT/'docs/test-evidence/M3/quality/jasmin-moghbeli-source.png';size=np.array(Image.open(source).size)
recipe=json.loads((OUT/'jasmin-moghbeli-production-recipe.json').read_text());p=recipe['people'][0]
pose=np.array(p['settings']['pose'])[:,:2]*(size-1);span=pose[12]-pose[11];width=np.linalg.norm(span);axis=span/width;vertical=np.array([-axis[1],axis[0]])
lm=np.array(p['landmarks'])[:,:2]
hair=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['settings']['automaticMasks']['hair']))).convert('L'));y,x=np.nonzero(hair>220);hair_points=np.stack([x[::5],y[::5]],axis=1)/np.array(hair.shape[::-1])
records=[]
fw,fh=p['face'][2:]
neck=np.array([lm[8]+[x*fw,y*fh] for x in np.linspace(-.25,.25,20) for y in np.linspace(.04,.55,22)])
skin=np.asarray(Image.open(io.BytesIO(base64.b64decode(p['settings']['automaticMasks']['skin']))).convert('L'));xy=(neck*np.array(skin.shape[::-1])).astype(int)
neck=neck[skin[xy[:,1],xy[:,0]]>220];assert len(neck)>30
for amount in [-100,100]:
    p['settings']['parameters']={'body.shoulderWidth.right':amount}
    r=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'single-field.bin'),render=str(OUT/f'jasmin-single-{amount}.png'))
    w,h=r['meshSize'];f=np.fromfile(OUT/'single-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
    drift=lambda points:float(np.linalg.norm((forward_points(f,points)-points)*(size-1),axis=1).max())
    cap=[];arm=[]
    for side in [0,1]:
        points=np.array([pose[11+side]+axis*x*width+vertical*y*width for x in [-.04,0,.04] for y in [-.04,0,.04]])/(size-1)
        shift=span*.06*amount/100 if side else np.zeros(2)
        cap.append(float(np.linalg.norm((forward_points(f,points)-points)*(size-1)-shift,axis=1).max()))
    shift=span*.06*amount/100
    for segment in [0,1]:
        first,last=pose[12+segment*2],pose[14+segment*2];v=last-first;normal=np.array([-v[1],v[0]])/np.linalg.norm(v)
        for t in [.25,.5,.75,1]:
            points=np.array([first+v*t+normal*width*c for c in [-.025,0,.025]])/(size-1)
            smooth=t*t*(3-2*t);weight=1-.5*smooth if segment==0 else .5*(1-smooth)
            arm.append(float(np.linalg.norm((forward_points(f,points)-points)*(size-1)-shift*weight,axis=1).max()))
    # Four corners of every bilinear cell, matching the production safety guard.
    dx=(f[:,1:]-f[:,:-1])*(w-1);dy=(f[1:]-f[:-1])*(h-1)
    jac=min(float(((1+a[:,:,0])*(1+b[:,:,1])-a[:,:,1]*b[:,:,0]).min()) for a in [dx[:-1],dx[1:]] for b in [dy[:,:-1],dy[:,1:]])
    record=dict(amount=amount,faceDrift=drift(lm),neckDrift=drift(neck),hairDrift=drift(hair_points),capError=max(cap),armError=max(arm),minimumJacobian=jac)
    records.append(record);print(record,flush=True)
(OUT/'single-surface.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
assert all(max(r['faceDrift'],r['neckDrift'],r['hairDrift'],r['capError'],r['armError'])<.5 and r['minimumJacobian']>.05 for r in records)
