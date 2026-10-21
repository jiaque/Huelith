"""Visual coverage of volume, brows, hair, line materials and removal."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
import copy,base64,io
OUT=ROOT/'build/interaction-effects';OUT.mkdir(exist_ok=True)
source=ROOT/'research/m3/portraits/jassi-standing.jpg'
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
size=list(Image.open(source).size)
call(op='init',settings=str(OUT/'settings'))
results=[]
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,workspaceTask=0);wait()
 def render(name):
  state=wait();call(op='portrait_geometry',recipe=state['recipe'],source=str(source),size=size,render=str(OUT/(name+'.png')));return state,np.array(Image.open(OUT/(name+'.png'))).astype(float)
 _,original=render('original')
 for mode,count in [(0,7),(1,6)]:
  call(op='portrait_state',volumeMode=mode);wait()
  for card in range(1,count):
   call(op='portrait_state',click='portraitVolumeCard'+str(card));wait();call(op='portrait_state',portraitVolumeAmount=80)
   state,arr=render(f'volume-{mode}-{card}');diff=abs(arr-original).max(2);measure={'mode':mode,'card':card,'changedPixels':int((diff>1).sum()),'peak':float(diff.max()),'status':state['status']};results.append(measure);assert measure["changedPixels"]>0,measure;print(measure,flush=True)
   call(op='portrait_state',portraitVolumeAmount=0);wait()
 call(op='portrait_close')
 # Physical eyebrow rotation: render true geometry and quantify at the brow landmarks.
 brow=copy.deepcopy(recipe);s=brow['people'][0]['settings'];s['browGeometryVersion']=2;s['parameters']={'feature.browTilt.left':100,'feature.browTilt.right':100}
 info=call(op='portrait_geometry',recipe=brow,size=size,field=str(OUT/'brow.bin'),source=str(source),render=str(OUT/'brow.png'))
 w,h=info['meshSize'];field=np.fromfile(OUT/'brow.bin',np.float32).reshape(h,w,4);print('Brow max displacement',abs(field[:,:,:2]).max(axis=(0,1))*np.array(size),flush=True)
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,workspaceTask=1);wait();call(op='portrait_state',click='portraitHairCard3');wait()
 # Use a point just below a real scalp hair boundary, no Alt sampling.
 hair=np.array(Image.open(io.BytesIO(base64.b64decode(recipe['people'][0]['settings']['automaticMasks']['hair']))));yy,xx=np.where(hair>220);target=[float(np.median(xx))/hair.shape[1],float(np.quantile(yy,.75))/hair.shape[0]]
 call(op='portrait_state',brushSize=20,paintPath=[target,[target[0],target[1]+.008]]);state,arr=render('hair-fill');assert state['recipe']['people'][0]['settings'].get('repairs'),state['status'];assert abs(arr-original).max()>2;hair_result=arr.copy();print('Hair fill without Alt',abs(arr-original).max(),flush=True)
 call(op='portrait_state',workspaceTask=10);wait();call(op='portrait_state',brushSize=35,paintPath=[[.75,.5],[.77,.5]]);state,arr=render('background-erase');assert state['recipe']['people'][0]['settings']['repairs'][-1]['imageRepair'];assert abs(arr-hair_result).max()>2;print('Background removal committed',flush=True)
 (OUT/'measurements.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
finally:call(op='shutdown')
