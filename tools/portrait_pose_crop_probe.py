import cv2 as cv,numpy as np,json,math
from pathlib import Path
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1];out=root/'docs/test-evidence/M3/semantic-shoulder'
net=cv.dnn.readNetFromONNX(str(root/'build/windows-release/models/pose_estimation_mediapipe_2023mar.onnx'))
name='jasmin-moghbeli';p=json.loads((root/'docs/test-evidence/M3/semantic-neck/jasmin-fresh-recipe.json').read_text())['people'][0]
im=cv.cvtColor(cv.imread(str(root/f'docs/test-evidence/M3/quality/{name}-source.png')),cv.COLOR_BGR2RGB);h,w=im.shape[:2];size=np.array([w,h]);body=np.array(p['body']);face=np.array(p['face']);center=(body[:2]+body[2:]*.5)*size;head=(face[:2]+face[2:]*.5)*size;extent=max(body[2:]*size)*1.25;delta=head-center;angle=math.degrees(math.atan2(delta[0],-delta[1]));lm=np.array(p['landmarks'])[:,:2]*size;expected=np.array([lm[30],lm[36:42].mean(0),lm[42:48].mean(0)]);rows=[]
for shift in [0,.2,.4]:
 for scale in [.65,.8,1,1.25,1.6]:
  for theta in [angle,0]:
   c=center+np.array([0,extent*shift]);matrix=cv.getRotationMatrix2D(tuple(c),theta,256/(extent*scale));matrix[:,2]+=128-c
   crop=cv.warpAffine(im,matrix,(256,256));net.setInput(crop.astype(np.float32)[None]/255);outputs=net.forward(net.getUnconnectedOutLayersNames());raw=next(o for o in outputs if o.size==195).reshape(-1,5)[:33];confidence=1/(1+np.exp(-np.clip(raw[:,3:],-30,30)));points=np.c_[raw[:,:2],np.ones(33)]@cv.invertAffineTransform(matrix).T
   eyes=points[[2,5]];eyes=eyes[np.argsort(eyes[:,0])];truth=expected[1:];truth=truth[np.argsort(truth[:,0])];err=(np.linalg.norm(points[0]-expected[0])+np.linalg.norm(eyes-truth,axis=1).sum())/3
   quality=float(confidence[[11,12,23,24]].prod(axis=1).mean());rows.append((err,shift,scale,theta,quality,points.tolist(),confidence.prod(axis=1).tolist()))
rows.sort(key=lambda r:r[0]);print([(r[:5]) for r in rows[:10]])
for i,row in enumerate(rows[:3]):
 img=Image.fromarray(im);d=ImageDraw.Draw(img);points=np.array(row[5]);
 for j in [0,11,12,13,14,15,16,23,24]:
  x,y=points[j];d.ellipse((x-4,y-4,x+4,y+4),fill='red');d.text((x,y),str(j),fill='white')
 img.save(out/f'pose-candidate-{i}.png')
(out/'pose-candidates.json').write_text(json.dumps(rows))
