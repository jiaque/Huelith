"""C05 grouped workspace, shared state, nail worker and project validation."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
import copy
OUT=ROOT/'docs/test-evidence/M3/workspace-c05';OUT.mkdir(exist_ok=True)
os.environ['TEMP']=os.environ['TMP']=str(ROOT/'build/test-temp')
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png'
recipe=json.loads((source.parent/'leroy-chiao-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
call(op='init',settings=str(OUT/'settings'))
def group(g):
 s=call(op='portrait_state',control=f'portraitMakeupGroup{g}')
 if not s['control']['checked']:call(op='portrait_state',click=f'portraitMakeupGroup{g}');wait()
def page(g,p):
 group(g)
 if g<6:call(op='portrait_state',makeupPage=p);wait()
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=2,select=0);wait()
 keys=[['makeup.lip','makeup.lipGloss','makeup.lipFeather'],['makeup.browShape','makeup.brow'],['makeup.shadow','makeup.liner','makeup.lash','makeup.aegyosal','makeup.iris','makeup.catchlight'],['makeup.blush','makeup.blush','makeup.blush'],['contour.highlight','contour.highlight','contour.shadow'],['makeup.mole','makeup.freckles',None,None],[None]]
 number=11
 for g,entries in enumerate(keys):
  for n,key in enumerate(entries):
   page(g,n)
   if key:call(op='portrait_state',parameter=key,value=35);wait()
   state=call(op='portrait_state',workspaceLayout=True,capture=str(OUT/f'{number:03}.png'))
   headings=[x for x in state['visibleControls'] if x.startswith('portraitMakeupGroup') and 'Eye' not in x]
   assert len(headings)==9 and len(set(headings))==9,(g,n,headings)
   assert state['acceptVisible']
   number+=1
 # Both PNG categories use actual portable instances, independent transforms and masks.
 for page_index,asset in [(2,'blue-flower'),(3,'botanical-tattoo')]:
  page(5,page_index);call(op='portrait_state',click='portraitDecalAdd_'+asset);state=wait()
  decal=state['recipe']['people'][0]['settings']['decals'][-1];quad=np.array(decal['quad']);center=quad.mean(0)
  call(op='portrait_state',paintPath=[center.tolist(),(center+[.01,.01]).tolist()]);state=wait()
  assert np.linalg.norm(np.array(state['recipe']['people'][0]['settings']['decals'][-1]['quad'])-quad)>.005
  call(op='portrait_state',click='portraitDecalAction1',brushSize=30,paint=center.tolist());state=wait()
  assert 'decal.'+decal['id'] in state['recipe']['people'][0]['settings']['masks']
  before=state['recipe'];call(op='portrait_state',click='portraitDecalAction2');wait();call(op='portrait_state',history=-1);assert wait()['recipe']==before
 # Color on one side must not overwrite the other; cards preserve previous lip color.
 page(0,0);call(op='portrait_state',click='portraitMakeupColor_makeup.lip_b54861');before=wait()['recipe']['people'][0]['settings']['colors']
 page(0,1);call(op='portrait_state',click='portraitMakeupCard_lipTexture_glass');assert wait()['recipe']['people'][0]['settings']['colors']==before
 page(2,0);call(op='portrait_state',linked=False,side=0);call(op='portrait_state',click='portraitShadowDetails');call(op='portrait_state',click='portraitMakeupColor_makeup.shadow_986c55');s=wait()['recipe'];assert 'makeup.shadow.left' in s['people'][0]['settings']['colors']
 call(op='portrait_state',side=1);call(op='portrait_state',click='portraitShadowDetails');call(op='portrait_state',click='portraitMakeupColor_makeup.shadow_8b6877');s=wait()['recipe'];colors=s['people'][0]['settings']['colors'];assert colors['makeup.shadow.left']!=colors['makeup.shadow.right']
 call(op='portrait_state',workspaceTask=3);wait();call(op='portrait_state',workspaceTask=2);assert wait()['recipe']==s
 r=call(op='portrait_state',source=str(source),save=str(OUT/'makeup.huelith'),render=str(OUT/'combined.png'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
 saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'makeup.huelith'));assert wait()['recipe']==saved;call(op='portrait_close')
 # Real C++ worker, then actual workspace detect/apply/select/recolor/save.
 hand=ROOT/'build/nail-evaluation/hand-2.jpg';r=copy.deepcopy(recipe);person=r['people'][0];person['settings']={'partialSkin':True,'parameters':{}};person['landmarks']=[];person['face']=[0,0,1,1];person['body']=[0,0,1,1];person.pop('bodyMask',None)
 call(op='portrait_window',path=str(hand),recipe=r,models=str(ROOT/'build/windows-release/models'));r['sourceHash']=wait()['recipe']['sourceHash'];call(op='portrait_close');call(op='portrait_window',path=str(hand),recipe=r,models=str(ROOT/'build/windows-release/models'));wait();call(op='portrait_state',workspaceTask=2,select=0);wait();group(6)
 call(op='portrait_state',click='portraitNailDetect');state=wait();nails=state['recipe']['people'][0]['settings'].get('nails',[]);assert len(nails)==5,state['status']
 call(op='portrait_state',capture=str(OUT/'nails-auto.png'))
 # Select by actual mask interior rather than only the instance list.
 import base64,io
 mask=np.array(Image.open(io.BytesIO(base64.b64decode(nails[3]['mask']))));ys,xs=np.where(mask>200);q=[float(xs[len(xs)//2]/mask.shape[1]),float(ys[len(ys)//2]/mask.shape[0])]
 call(op='portrait_state',click='portraitNailSelect',paint=q);wait();call(op='portrait_state',click='portraitNailColor_416179');colored=wait()['recipe'];after=colored['people'][0]['settings']['nails'];assert after[3]['color']!=nails[3]['color'] and all(after[i]==nails[i] for i in [0,1,2,4])
 call(op='portrait_state',history=-1);assert wait()['recipe']['people'][0]['settings']['nails']==nails
 call(op='portrait_state',history=1);assert wait()['recipe']==colored
 # A missing automatic candidate remains editable as a manually outlined instance.
 call(op='portrait_state',click='portraitNailOutline',paintPath=[[.455,.26],[.51,.25],[.52,.28],[.48,.30],[.455,.26]])
 manual=wait()['recipe']['people'][0]['settings']['nails'];assert len(manual)==6 and manual[-1]['origin']=='manual'
 call(op='portrait_state',click='portraitNailRemove');removed=wait()['recipe']
 assert removed['people'][0]['settings']['nails']==colored['people'][0]['settings']['nails']
 assert removed['people'][0]['settings']['parameters']==colored['people'][0]['settings']['parameters']
 colored=removed
 r=call(op='portrait_state',source=str(hand),save=str(OUT/'nails.huelith'),render=str(OUT/'nails-color.png'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
 call(op='portrait_close');call(op='portrait_window',project=str(OUT/'nails.huelith'));assert wait()['recipe']==colored
 print('C05 22 pages, repeated layout, sided colors, material independence, cross-entry, nails worker/selection and save passed')
finally:call(op='portrait_close');call(op='shutdown')
