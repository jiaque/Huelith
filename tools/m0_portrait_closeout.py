"""Fixed M0 portrait risk cases; save failures rather than expand the dataset."""
import json
from pathlib import Path
import hashlib
import cv2
import numpy as np
from PIL import Image, ImageDraw
from m0_workbench import detect_people

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M0/closeout/portrait'
OUT.mkdir(parents=True,exist_ok=True)
first=Image.open(ROOT/'research/m0/portrait-highres.jpg').convert('RGB')
second=Image.open(ROOT/'research/m0/portrait-glenn.jpg').convert('RGB')
a=first.copy();a.thumbnail((700,850))
b=second.copy();b.thumbnail((700,850))
pair=Image.new('RGB',(1400,850),'#777777');pair.paste(a,(0,0));pair.paste(b,(700,0))
occluded=a.copy();draw=ImageDraw.Draw(occluded)
draw.rectangle((int(a.width*.30),int(a.height*.14),int(a.width*.58),int(a.height*.22)),fill='#555555')
variants={'collins':a,'glenn':b,'two_different_people':pair,
          'collins_rotate15':a.rotate(15,expand=False,fillcolor='#777777'),
          'collins_occluded':occluded}
results=[]
for name,image in variants.items():
    boxes=detect_people(image)
    marked=image.copy();draw=ImageDraw.Draw(marked)
    for box in boxes:draw.rectangle(box,outline='#00ff99',width=3)
    marked.save(OUT/(name+'-detection.png'))
    results.append(dict(name=name,faces=len(boxes),boxes=boxes))

# Actual fine line under the right eye, deliberately selected by hand.
face=np.asarray(first.crop((830,630,1420,1110))).copy()
mask=np.zeros(face.shape[:2],np.uint8)
cv2.polylines(mask,[np.array([[378,139],[400,144],[425,146],[442,149]],np.int32)],False,255,3)
encoded=face.astype(np.float32)/255
linear=np.where(encoded<=.04045,encoded/12.92,((encoded+.055)/1.055)**2.4).astype(np.float32)
repaired=np.stack([cv2.inpaint(linear[:,:,i],mask,3,cv2.INPAINT_NS) for i in range(3)],axis=-1)
outside_unchanged=bool(np.array_equal(repaired[mask==0],linear[mask==0]))
if not outside_unchanged:raise ValueError('repair changed pixels outside the selected range')
srgb=np.where(repaired<=.0031308,repaired*12.92,1.055*np.maximum(repaired,0)**(1/2.4)-.055)
after=np.uint8(np.clip(srgb,0,1)*255+.5)
selected=face.copy();selected[mask>0]=[255,50,50]
Image.fromarray(np.concatenate([face,selected,after],axis=1)).save(OUT/'real-line-repair.png')

# Manual torso range: a compact ellipse inside the suit; surrounding background protected.
body=np.asarray(a).copy();h,w=body.shape[:2]
yy,xx=np.indices((h,w),dtype=np.float32)
distance=((xx-w*.36)/(w*.15))**2+((yy-h*.60)/(h*.17))**2
weight=np.maximum(0,1-distance)**2
changed=cv2.remap(body,xx+8*weight,yy,cv2.INTER_LINEAR,borderMode=cv2.BORDER_REFLECT_101)
protected=weight==0
background_unchanged=bool(np.array_equal(changed[protected],body[protected]))
if not background_unchanged:raise ValueError('protected area moved')
Image.fromarray(np.concatenate([body,changed],axis=1)).save(OUT/'torso-protection.png')
report=dict(detection=results,
    real_line_repair=dict(range_pixels=int(np.count_nonzero(mask)),outside_unchanged=outside_unchanged,
        clean_ground_truth=False,manual_coordinates=[[378,139],[400,144],[425,146],[442,149]]),
    torso=dict(max_displacement_pixels=8,protected_area_unchanged=background_unchanged,range='manual soft ellipse'),
    inputs=[dict(file=p,sha256=hashlib.sha256((ROOT/'research/m0'/p).read_bytes()).hexdigest())
            for p in ['portrait-highres.jpg','portrait-glenn.jpg']],
    decision='Keep explicit manual ranges; do not approve automatic wrinkle/blemish removal or body association.',
    limitations=['Two NASA portraits and three derived cases only; not population accuracy.',
        'In-plane rotation is not a true side-profile test.',
        'Real-line repair has no clean reference: visual review only, no quality PSNR.',
        'Torso range is manually defined, not pose/instance recognition.'])
(OUT/'closeout.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report),flush=True)
