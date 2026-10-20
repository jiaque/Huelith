"""Evaluate overlapping, aspect-preserving detection crops, without changing product."""
from pathlib import Path
import json,cv2,numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M3/blemish-model/scale-review';OUT.mkdir(parents=True,exist_ok=True)
net=cv2.dnn.readNetFromONNX(str(ROOT/'research/m3/blemish-model-evaluation/blemish_detection_modelscope.onnx'))
cv2.setNumThreads(4)
records=[]
for name in ['acne-oily','acne-face','john-glenn']:
    src=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png'
    rgb=np.asarray(Image.open(src).convert('RGB'));h,w=rgb.shape[:2]
    p=json.loads(src.with_name(name+'-recipe.json').read_text())['people'][0]
    x,y,fw,fh=p['face'];x0=max(0,int((x-.2*fw)*w));y0=max(0,int((y-.2*fh)*h));x1=min(w,int((x+1.2*fw)*w));y1=min(h,int((y+1.2*fh)*h))
    side=min(x1-x0,y1-y0);output=np.zeros((h,w),np.float32)
    # Square face-scale crops retain aspect; half-size crops test fine-detail recall.
    for fraction in [1.,.65]:
        length=max(64,int(side*fraction));step=max(1,int(length*.65))
        xs=sorted(set(list(range(x0,max(x0+1,x1-length+1),step))+[max(x0,x1-length)]))
        ys=sorted(set(list(range(y0,max(y0+1,y1-length+1),step))+[max(y0,y1-length)]))
        for top in ys:
            for left in xs:
                crop=rgb[top:top+length,left:left+length];ch,cw=crop.shape[:2]
                a=cv2.resize(crop,(768,768)).astype(np.float32)/127.5-1
                net.setInput(a.transpose(2,0,1)[None]);logit=net.forward()[0,0];prob=1/(1+np.exp(-np.clip(logit,-60,60)))
                prob=cv2.resize(prob,(cw,ch),interpolation=cv2.INTER_NEAREST)
                # Discard crop boundary responses unless this is an image boundary.
                margin=max(1,int(length*.08))
                if left>x0:prob[:,:margin]=0
                if top>y0:prob[:margin,:]=0
                if left+cw<x1:prob[:,-margin:]=0
                if top+ch<y1:prob[-margin:,:]=0
                output[top:top+ch,left:left+cw]=np.maximum(output[top:top+ch,left:left+cw],prob)
    Image.fromarray(np.uint8(np.rint(output*255))).save(OUT/(name+'-evidence.png'))
    regions=[(.37636,.15181,.02),(.631,.673,.02)] if name=='acne-oily' else [(.468,.203,.035),(.774,.242,.04),(.113,.454,.025)] if name=='acne-face' else [(.48663,.31885,.006)]
    row=[]
    yy,xx=np.mgrid[:h,:w]
    for x,y,r in regions:
        mask=(xx/w-x)**2+(yy/h-y)**2<=r*r
        row.append(dict(x=x,y=y,max=float(output[mask].max()),fractionAbove035=float((output[mask]>=.35).mean())))
    records.append(dict(name=name,regions=row));print(records[-1],flush=True)
(OUT/'results.json').write_text(json.dumps(records,indent=2))
