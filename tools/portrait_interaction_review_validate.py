"""Grouped regressions for the reported editing workflow, using real Qt events."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
import copy
OUT=ROOT/'build/interaction-review';OUT.mkdir(exist_ok=True)
source=ROOT/'research/m3/portraits/jassi-standing.jpg'
base=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text())
for p in base['people']:p['settings']['parameters']={};p['settings'].pop('repairs',None);p['settings'].pop('liquify',None)
size=list(Image.open(source).size)
call(op='init',settings=str(OUT/'settings'))
def open_window(recipe=base):
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0);wait()
def group(g):
 state=call(op='portrait_state',control=f'portraitMakeupGroup{g}')
 if not state['control']['checked']:call(op='portrait_state',click=f'portraitMakeupGroup{g}');wait()
def render(name):
 s=wait();call(op='portrait_geometry',recipe=s['recipe'],size=size,source=str(source),render=str(OUT/(name+'.png')));return s
try:
 # Deformation must not introduce horizontal drift when lengthening vertically.
 for key,version in [('body.height','heightVersion'),('body.legLength','legLengthVersion')]:
  recipe=copy.deepcopy(base);s=recipe['people'][0]['settings'];s[version]=2
  s['parameters']={key:80} if key=='body.height' else {key+'.left':80,key+'.right':80}
  info=call(op='portrait_geometry',recipe=recipe,size=size,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/(key+'.png')))
  w,h=info['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)
  assert abs(field[:,:,0]).max()<1e-6,(key,'horizontal drift',abs(field[:,:,0]).max())
  assert abs(field[:,:,1]).max()>1e-4,key
 print('Vertical body extension verified',flush=True)
 open_window();call(op='portrait_state',workspaceTask=8,click='portraitBodyGroup5');wait()
 call(op='portrait_state',bodySide=1,parameter='body.chest',value=45);s=render('chest')
 assert s['recipe']['people'][0]['settings']['parameters'].get('body.chest.left')==45,s['status']
 call(op='portrait_state',workspaceTask=9);s=wait()
 assert 'portraitSlimTabs' not in s.get('visibleControls',[])
 call(op='portrait_state',click='portraitCanvasFreeze7',brushSize=100,paintPath=[[.5,.5],[.52,.5]]);wait()
 call(op='portrait_state',click='portraitCanvasFreeze8',paintPath=[[.5,.5],[.52,.5]]);wait()
 call(op='portrait_state',click='portraitCanvasFreeze0',paintPath=[[.48,.5],[.50,.5]]);s=wait();assert s['recipe']['people'][0]['settings'].get('liquify')
 call(op='portrait_state',click='portraitCanvasHand');wait();call(op='portrait_state',click='portraitCanvasBrush');wait()
 print('Chest, freeze/thaw/push and hand switching verified',flush=True)
 call(op='portrait_state',workspaceTask=2);wait();group(1)
 call(op='portrait_state',click='portraitMakeupCard_browMaterial_straight');wait();call(op='portrait_state',parameter='makeup.browShape',value=100);wait()
 call(op='portrait_state',click='portraitMakeupCard_browMaterial_arched');s=wait();assert s['recipe']['people'][0]['settings']['parameters']['makeup.browShape.left']==50
 call(op='portrait_state',click='portraitMakeupCard_browMaterial_straight');s=wait();assert s['recipe']['people'][0]['settings']['parameters']['makeup.browShape.left']==100
 group(3);call(op='portrait_state',click='portraitMakeupColor_makeup.blush_b54861');wait();c=call(op='portrait_state',control='portraitMakeupColor_makeup.blush_b54861');assert c['control']['checked']
 call(op='portrait_state',parameter='makeup.blush',value=100);render('blush')
 print('Style strengths and selected colour verified',flush=True)
 call(op='portrait_close')
 # A synthetic mole is a real material; a later healing stroke must change its output.
 recipe=copy.deepcopy(base);s=recipe['people'][0]['settings'];s['parameters']={'makeup.mole':100,'makeup.moleSize':70}
 open_window(recipe);initial=render('mole-before');call(op='portrait_state',workspaceTask=5);wait()
 # Locate the actual pigment in the rendered output, not a generic effect mask.
 call(op='portrait_geometry',recipe=base,size=size,source=str(source),render=str(OUT/'mole-clean.png'))
 clean=np.array(Image.open(OUT/'mole-clean.png')).astype(float);pigmented=np.array(Image.open(OUT/'mole-before.png')).astype(float)
 delta=abs(clean-pigmented).max(2);assert delta.max()>5
 y,x=np.unravel_index(delta.argmax(),delta.shape);point=[x/size[0],y/size[1]]
 call(op='portrait_state',brushSize=12,paintPath=[point,[point[0]+.001,point[1]]]);s=render('mole-after')
 repairs=s['recipe']['people'][0]['settings'].get('repairs',[]);assert repairs and repairs[-1]['afterStyle'],s['status']
 a=np.array(Image.open(OUT/'mole-before.png')).astype(float);b=np.array(Image.open(OUT/'mole-after.png')).astype(float)
 assert abs(b-clean)[delta>5].mean()<abs(a-clean)[delta>5].mean()*.6,'mole pigment not reduced'
 r=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'combined.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
 print('Repair after makeup, save/reopen and regional output verified',flush=True)
finally:
 call(op='shutdown')
