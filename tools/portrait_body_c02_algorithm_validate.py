from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
import copy,base64,io
from PIL import ImageFilter
OUT=ROOT/'docs/test-evidence/M3/workspace-c02';source=ROOT/'research/m3/portraits/jassi-standing.jpg';size=Image.open(source).size
base=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text());settings=base['people'][0]['settings'];settings['parameters']={};settings['manualScaleRegion']=[.46,.50,.08,.22];settings['protectBackground']=False
call(op='init',settings=str(OUT/'algorithm-settings'))
try:
 fields=[]
 for aspect in [True,False]:
  settings['parameters']={'body.manualScale':65};settings['manualScaleKeepAspect']=aspect
  r=call(op='portrait_geometry',size=size,recipe=base,field=str(OUT/'field.bin'));w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2];fields.append(f.copy())
 assert abs(fields[1][:,:,1]).max()<1e-6 and abs(fields[0][:,:,1]).max()>.001
 settings['parameters']={};call(op='portrait_geometry',size=size,recipe=base,source=str(source),render=str(OUT/'line-base.png'));original=np.array(Image.open(OUT/'line-base.png')).astype(float)
 settings['parameters']={'contour.lines':100};settings['bodyLinesQuad']=[[.30,.31],[.62,.31],[.62,.46],[.30,.46]]
 skin=np.array(Image.open(io.BytesIO(base64.b64decode(settings['automaticMasks']['skin']))).resize(size))
 images=[]
 for material in ['collarbone','abdomen','waist']:
  settings['bodyMaterial']=material;call(op='portrait_geometry',size=size,recipe=base,source=str(source),render=str(OUT/f'line-{material}.png'));im=np.array(Image.open(OUT/f'line-{material}.png')).astype(float);images.append(im)
  delta=abs(im-original);assert delta.max()>1,material;assert delta[np.array(Image.fromarray((skin>0).astype('uint8')*255).filter(ImageFilter.MaxFilter(15)))==0].max()<=1,(material,'clothing changed')
 assert all(np.abs(images[i]-images[j]).sum()>100 for i,j in [(0,1),(1,2),(0,2)])
 # Shared natural combination includes the visible waist, preserves previous head, then body/face/liquify can alternate.
 settings['parameters']={'head.size':-12};call(op='portrait_window',path=str(source),recipe=base);wait();call(op='portrait_state',workspaceTask=8,select=0,click='portraitBodyNatural');s=wait();params=s['recipe']['people'][0]['settings']['parameters'];assert params['head.size']==-12 and params.get('body.waist',0)==9
 call(op='portrait_state',click='portraitPresetApply');wait();call(op='portrait_state',click='portraitBodyGroup4');wait();call(op='portrait_state',bodySide=1,parameter='body.legWidth',value=25);s=wait();assert s['recipe']['people'][0]['settings']['parameters']['body.legWidth.left']==25
 call(op='portrait_state',workspaceTask=3,parameter='face.width',value=12);s=wait();assert s['recipe']['people'][0]['settings']['parameters']['face.width']==12
 call(op='portrait_state',workspaceTask=9,slimMode=1,liquifyTool=0,brushSize=70,paintPath=[[.46,.19],[.465,.19]]);wait();call(op='portrait_state',workspaceTask=8,bodyMode=1,bodyTool=2,parameter='body.manualScale',value=10);s=wait();assert s['recipe']['people'][0]['settings']['parameters']['body.manualScale']==10
 print('C02 actual material differences, skin/clothes boundary, aspect behavior and body-face-liquify-body passed')
finally:call(op='portrait_close');call(op='shutdown')
