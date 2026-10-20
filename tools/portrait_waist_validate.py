from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-waist';OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg';recipe=json.loads((source.parent/'mediapipe-pose-recipe.json').read_text(encoding='utf-8'))
call(op='init',settings=str(OUT/'settings'));p=recipe['people'][0];p['settings']['waistGeometryVersion']=2
pose=np.array(p['settings']['pose']);size=np.array(Image.open(source).size)
a=pose[[11,12],:2].mean(0)*size;b=pose[[23,24],:2].mean(0)*size;axis=b-a;axis/=np.linalg.norm(axis);normal=np.array([-axis[1],axis[0]])
mid=a+(b-a)*.625;width=np.linalg.norm((pose[11,:2]-pose[12,:2])*size)*.25;probes=np.array([mid-normal*width,mid+normal*width])/size
for value in [-100,0,100]:
 p['settings']['parameters']={'body.waist':value}
 r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'waist-{value}.png'))
 if value==0:assert r['identity'];continue
 mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
 joints=pose[[11,12,13,14,15,16,23,24],:2];drift=np.linalg.norm((forward_points(field,joints)-joints)*size,axis=1).max();assert drift<.75,drift
 output=forward_points(field,probes)*size;ratio=np.linalg.norm(output[1]-output[0])/(width*2);assert (ratio-1)*value<0,ratio
 print(value,ratio,drift,flush=True)
call(op='shutdown')
