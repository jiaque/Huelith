"""C07 eye workspace: shared shape/materials, sided repair, masks and persistence."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
import copy,base64,io
os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe')
os.environ['TEMP']=os.environ['TMP']=str(ROOT/'build/test-temp')
OUT=ROOT/'docs/test-evidence/M3/workspace-c07';OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png'
recipe=json.loads(source.with_name('jessica-watkins-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
def mask(values):
 b=io.BytesIO();Image.fromarray(np.uint8(values)).save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=14,select=0);state=wait()
 assert 'eye.deRed' in state['recipe']['people'][0]['settings']['automaticMasks']
 call(op='portrait_state',linked=False,side=0,parameter='eye.circles',value=35);wait();call(op='portrait_state',side=1,parameter='eye.circles',value=15);state=wait()
 params=state['recipe']['people'][0]['settings']['parameters'];assert params['eye.circles.left']==35 and params['eye.circles.right']==15
 call(op='portrait_state',capture=str(OUT/'088.png'))
 call(op='portrait_state',eyeMode=1);wait();call(op='portrait_state',eyeTarget=0,side=0);s=wait();lm=np.array(s['recipe']['people'][0]['landmarks'])[:,:2];left=(lm[42]+lm[45])/2+[0,.01];right=(lm[36]+lm[39])/2+[0,.01]
 call(op='portrait_state',brushSize=12,paint=left.tolist());s=wait();assert 'eye.circles.left' in s['recipe']['people'][0]['settings']['masks']
 savedLeft=s['recipe']['people'][0]['settings']['masks']['eye.circles.left']
 call(op='portrait_state',side=1,paint=right.tolist());s=wait();m=s['recipe']['people'][0]['settings']['masks'];assert m['eye.circles.left']==savedLeft and 'eye.circles.right' in m
 before=s['recipe'];call(op='portrait_state',click='portraitEyeErase',paint=right.tolist());erased=wait()['recipe'];assert erased!=before
 call(op='portrait_state',history=-1);assert wait()['recipe']==before
 call(op='portrait_state',history=1);assert wait()['recipe']==erased
 call(op='portrait_state',linked=True);wait();call(op='portrait_state',click='portraitEyePaint',paint=left.tolist());both=wait()['recipe'];m=both['people'][0]['settings']['masks'];assert 'eye.circles' in m and 'eye.circles.left' not in m and 'eye.circles.right' not in m
 call(op='portrait_state',capture=str(OUT/'089.png'));call(op='portrait_state',eyeMode=0);wait()
 call(op='portrait_state',click='portraitEyeWrinkles');wait();assert call(op='portrait_state',workspaceLayout=True)['workspaceTask']==6
 call(op='portrait_state',workspaceTask=3,panel=1,linked=True,parameter='feature.eyeSize',value=12);shape=wait()['recipe'];call(op='portrait_state',capture=str(OUT/'090.png'))
 call(op='portrait_state',workspaceTask=3,panel=1);assert wait()['recipe']==shape
 call(op='portrait_state',workspaceTask=2);wait();call(op='portrait_state',click='portraitMakeupGroup2');wait();call(op='portrait_state',makeupPage=4);wait();state=call(op='portrait_state',workspaceLayout=True)
 cards=[n for n in state['visibleControls'] if n.startswith('portraitMakeupCard_irisPalette_')];assert len(cards)==8,cards
 call(op='portrait_state',click='portraitMakeupCard_irisPalette_3',parameter='makeup.iris',value=25);materials=wait()['recipe'];call(op='portrait_state',capture=str(OUT/'091.png'))
 call(op='portrait_state',workspaceTask=2);assert wait()['recipe']==materials
 call(op='portrait_state',workspaceTask=14);assert wait()['recipe']==materials
 r=call(op='portrait_state',source=str(source),save=str(OUT/'eyes.huelith'),render=str(OUT/'combined.png'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'eyes.huelith'));assert wait()['recipe']==saved;call(op='portrait_close')
 # Closed/occluded iris explicitly disables only lens/catchlight pages.
 closed=copy.deepcopy(saved);s=closed['people'][0]['settings']
 for side in ['.left','.right']:s['irisMaterials']['geometry'+side]['valid']=False
 call(op='portrait_window',path=str(source),recipe=closed);wait();call(op='portrait_state',workspaceTask=2);wait();call(op='portrait_state',click='portraitMakeupGroup2');wait();call(op='portrait_state',makeupPage=4);wait();assert call(op='portrait_state',control='portraitEyeUnavailable')['control']['visible']
 call(op='portrait_state',makeupPage=0);wait();assert call(op='portrait_state',control='portraitMakeupCard_shadowPalette_0')['control']['visible'];call(op='portrait_close')
 # Controlled masks distinguish red sclera from flash-red pupils, with independent sides.
 r=copy.deepcopy(recipe);p=r['people'][0];p['settings']={'parameters':{},'eyeWhiteVersion':2,'redEyeVersion':2,'automaticMasks':{},'masks':{}}
 lm=np.array(p['landmarks']);lm[36,:2]=[.1,.5];lm[39,:2]=[.4,.5];lm[42,:2]=[.6,.5];lm[45,:2]=[.9,.5];p['landmarks']=lm.tolist()
 im=Image.new('RGB',(128,64),(200,140,140));ImageDraw.Draw(im).rectangle((52,0,75,63),fill=(220,35,30));src=OUT/'eye-swatches.png';im.save(src)
 support=np.full((64,128),255);support[:,52:76]=0;full=np.full((64,128),255);p['settings']['automaticMasks']['eye.scleraSupport']=mask(support)
 for key in ['eye.deRed','eye.white','eye.red']:p['settings']['masks'][key]=mask(full)
 frames={}
 for name,params in [('none',{}),('deRed',{'eye.deRed':100}),('white',{'eye.white':100}),('red',{'eye.red':100}),('left',{'eye.deRed.left':100,'eye.deRed.right':0})]:
  p['settings']['parameters']=params;dest=OUT/(name+'.bin');call(op='portrait_geometry',source=str(src),recipe=r,size=[128,64],linearRender=str(dest),render=str(dest.with_suffix('.png')));frames[name]=np.fromfile(dest,np.float32).reshape(64,128,4)
 base=frames['none'];weights=np.array([.2627,.678,.0593]);delta=frames['deRed']-base
 assert np.max(abs(delta[:,:,:3]@weights))<2e-6 and np.max(abs(delta[:,52:76]))<2e-6
 assert delta[30,25,0]<-.001 and frames['white'][30,25,:3]@weights>base[30,25,:3]@weights
 assert frames['red'][30,60,:3]@weights<base[30,60,:3]@weights
 assert np.max(abs(frames['left'][:,:50]-base[:,:50]))<2e-6 and np.max(abs(frames['left'][:,80:]-base[:,80:]))>.001
 p['settings']['automaticMasks']['eyeglassFrame']=mask(full);p['settings']['parameters']={'eye.deRed':100};dest=OUT/'frame.bin';call(op='portrait_geometry',source=str(src),recipe=r,size=[128,64],linearRender=str(dest),render=str(dest.with_suffix('.png')));assert np.max(abs(np.fromfile(dest,np.float32).reshape(base.shape)-base))<2e-6
 # Real moderate turn and eyeglasses: inspect a before/after board and coverage.
 for name in ['jessica-watkins','glasses','ellen-ochoa']:
  src=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';rr=json.loads(src.with_name(name+'-recipe.json').read_text());person=rr['people'][0];settings=person['settings'];settings['parameters']={};settings['masks']={}
  analysis=call(op='portrait_geometry',repairAnalysis=True,source=str(src),person=person)
  for key in ['eye.circles','eye.bags','eye.white','eye.deRed','eye.scleraSupport','eye.red','eyeglassFrame']:settings.setdefault('automaticMasks',{})[key]=analysis[key]
  settings.update(eyeWhiteVersion=2,redEyeVersion=2);size=list(Image.open(src).size);frames=[]
  for value in [0,65]:
   settings['parameters']={'eye.circles':value,'eye.bags':value,'eye.white':value,'eye.deRed':value};dest=OUT/f'{name}-{value}.png';call(op='portrait_geometry',source=str(src),recipe=rr,size=size,render=str(dest));frames.append(Image.open(dest).copy())
  x,y,w,h=person['face'];bounds=(int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1]));cw,ch=bounds[2]-bounds[0],bounds[3]-bounds[1];board=Image.new('RGB',(cw*2,ch+24),(30,34,38));draw=ImageDraw.Draw(board)
  for i,frame in enumerate(frames):board.paste(frame.crop(bounds),(cw*i,24));draw.text((cw*i+5,4),'Before' if i==0 else 'Eye repair 65',fill='white')
  board.save(OUT/f'{name}-comparison.png')
  coverage=Image.open(io.BytesIO(base64.b64decode(analysis['eye.scleraSupport'])));coverage.save(OUT/f'{name}-sclera.png')
  if name=='jessica-watkins':assert np.asarray(coverage).max()>0
 print('C07 separated repair, shape and makeup, sided repair/brush, closed eyes, sclera/pupil separation, frame protection and save passed')
finally:call(op='portrait_close');call(op='shutdown')
