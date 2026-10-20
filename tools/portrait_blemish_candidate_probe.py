"""Diagnostic approximation of candidate rejection stages; not an acceptance test."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import cv2
import sys
OUT=ROOT/'docs/test-evidence/M3/semantic-blemish-categories'
name=sys.argv[1] if len(sys.argv)>1 else 'acne-face'
source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png'
recipe=json.loads((source.parent/'acne-face-recipe.json').read_text())
call(op='init',settings=str(OUT/'probe-settings'))
call(op='portrait_geometry',recipe=recipe,styleMask='skin',size=[1024,768],mask=str(OUT/'probe-skin.png'))
call(op='shutdown')
rgb=cv2.resize(np.asarray(Image.open(source).convert('RGB')),(1024,768),interpolation=cv2.INTER_AREA)
lab=cv2.cvtColor(rgb,cv2.COLOR_RGB2LAB).astype(np.float32);skin=np.asarray(Image.open(OUT/'probe-skin.png'))
strength=np.zeros(skin.shape,np.float32);types=np.zeros(skin.shape,np.uint8)
for scale in [.012,.025,.045]:
    low=cv2.GaussianBlur(lab,(0,0),max(1.5,1024*scale));red=lab[:,:,1]-low[:,:,1];dark=low[:,:,0]-lab[:,:,0];yellow=lab[:,:,2]-low[:,:,2]
    valid=(skin>230)&(lab[:,:,0]>=low[:,:,0]*.62)&(lab[:,:,0]<=low[:,:,0]+15)
    score=np.where(red>3.5,red/4,np.where((dark>7)&(dark<42)&(yellow>2)&(red>-.5),np.minimum(dark/9,(yellow+np.maximum(0,red))/3),0))
    kind=np.where(red>3.5,np.where(dark>6,1,2),3);update=valid&(score>strength);strength[update]=score[update];types[update]=kind[update]
binary=cv2.morphologyEx((strength>1).astype(np.uint8),cv2.MORPH_OPEN,cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(3,3)))
n,labels,stats,centers=cv2.connectedComponentsWithStats(binary)
targets=[[.47,.21],[.77,.24],[.11,.45]] if name=='acne-face' else [[.366,.146],[.632,.685],[.727,.417],[.511,.225]]
for k in [min(range(1,n),key=lambda k:np.linalg.norm(centers[k]-np.array(target)*[1024,768])) for target in targets]:
    x,y,w,h,area=stats[k];cx,cy=centers[k];radius=np.clip(max(w,h)*.6,2,1024*.055)
    kind=np.bincount(types[labels==k],minlength=4)[1:].argmax()+1;center=lab[int(cy),int(cx)];representative=np.median(lab[labels==k],axis=0);lights=[];basis=[];support=0;median_support=0
    neural=cv2.resize(np.asarray(Image.open(ROOT/f'docs/test-evidence/M3/blemish-model/real-review/{name}-evidence.png')),(1024,768),interpolation=cv2.INTER_LINEAR)
    print('model support',int(np.count_nonzero(neural[labels==k]>=90)),'of',int(area))
    for t in np.linspace(0,2*np.pi,24,endpoint=False):
        px,py=int(cx+np.cos(t)*radius*1.6),int(cy+np.sin(t)*radius*1.6)
        if not(0<=px<1024 and 0<=py<768) or skin[py,px]<230:continue
        ring=lab[py,px];lights.append(float(ring[0]));basis.append([1,np.cos(t),np.sin(t)]);support+=bool(ring[0]-center[0]>5 and center[2]-ring[2]>1) if kind==3 else bool(center[1]-ring[1]>2.5)
        median_support+=bool(ring[0]-representative[0]>5 and representative[2]-ring[2]>1) if kind==3 else bool(representative[1]-ring[1]>2.5)
    coef=np.linalg.lstsq(basis,lights,rcond=None)[0];residual=np.array(lights)-np.array(basis)@coef
    print('lighting',coef,'explained',1-np.var(residual)/max(1,np.var(lights)),'residual-range',np.quantile(residual,.9)-np.quantile(residual,.1))
    lights.sort();print(dict(center=[cx/1024,cy/768],area=int(area),width=int(w),height=int(h),kind=int(kind),ring=len(lights),support=support,medianSupport=median_support,centerLab=center.tolist(),medianLab=representative.tolist(),range=lights[int(len(lights)*.9)]-lights[int(len(lights)*.1)] if lights else None))
