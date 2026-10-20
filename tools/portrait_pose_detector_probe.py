from pathlib import Path
import sys,json,cv2 as cv,numpy as np
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'build/pose-detector-evaluation'))
from mp_persondet import MPPersonDet
from mp_pose import MPPose
# OpenCV 5 returns named outputs in a different order; match their contracts.
detPost=MPPersonDet._postprocess
MPPersonDet._postprocess=lambda self,outputs,*args:detPost(self,sorted(outputs,key=lambda a:-a.size),*args)
posePost=MPPose._postprocess
MPPose._postprocess=lambda self,outputs,*args:posePost(self,[next(o for o in outputs if o.size==n) for n in [195,1,65536,64*64*39,117]],*args)
out=root/'docs/test-evidence/M3/semantic-shoulder'
detector=MPPersonDet(str(root/'build/pose-detector-evaluation/person_detection_mediapipe_2023mar.onnx'))
pose=MPPose(str(root/'build/windows-release/models/pose_estimation_mediapipe_2023mar.onnx'))
for name in ['jasmin-moghbeli','jessica-watkins','christina-koch','leroy-chiao']:
 image=cv.imread(str(root/f'docs/test-evidence/M3/quality/{name}-source.png'));people=detector.infer(image);print(name,'detected',len(people),flush=True)
 for i,person in enumerate(people):
  result=pose.infer(image,person)
  if result is None:continue
  points=result[1];img=Image.fromarray(cv.cvtColor(image,cv.COLOR_BGR2RGB));draw=ImageDraw.Draw(img)
  for index in [0,11,12,13,14,15,16,23,24]:
   x,y=points[index,:2];draw.ellipse((x-4,y-4,x+4,y+4),fill='red');draw.text((x,y),str(index),fill='white')
  img.save(out/f'{name}-detector-{i}.png');(out/f'{name}-detector-{i}.json').write_text(json.dumps(dict(points=points.tolist(),detection=person.tolist())))
