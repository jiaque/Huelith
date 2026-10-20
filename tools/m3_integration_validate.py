"""Final M3 user operations through the existing Python/production DLL harness."""
from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
import copy
O=R/'docs/test-evidence/M3/integration';O.mkdir(parents=True,exist_ok=True)
source=R/'docs/test-evidence/M3/geometry/portrait-source.png'
base=json.loads((R/'docs/test-evidence/M3/quality/portrait-highres-recipe.json').read_text())
# Use matching source pixels/hash, with freshly identified geometry and no effects.
source=R/'docs/test-evidence/M3/quality/portrait-highres-source.png'
def open_recipe(recipe):
 checked(op='portrait_close')
 checked(op='portrait_window',path=str(source),recipe=recipe)
 checked(op='portrait_state',select=0)
def render(name):
 return checked(op='portrait_state',source=str(source),render=str(O/(name+'.png')),checkRegion=True)
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp)
 checked(op='portrait_window',path=str(source),recipe=base);checked(op='portrait_state',select=0,panel=5,parameter='skin.smooth',value=60)
 assert not render('smooth')['identity']
 checked(op='portrait_state',toggle='portraitEnabled_skin.smooth',checked=False)
 assert render('disabled')['identity']
 checked(op='portrait_state',toggle='portraitEnabled_skin.smooth',checked=True)
 assert not render('enabled')['identity']
 open_recipe(base);checked(op='portrait_state',panel=0,parameter='face.width',value=60)
 render('shape');checked(op='portrait_state',source=str(source),render=str(O/'neutral.png'),amount=0)
 face=base['people'][0]['face'];center=[face[0]+face[2]*.5,face[1]+face[3]*.5]
 checked(op='portrait_state',click='portraitFreeze1',brushSize=500,paint=center)
 render('frozen')
 neutral=np.array(Image.open(O/'neutral.png')).astype(float)
 before=np.abs(np.array(Image.open(O/'shape.png')).astype(float)-neutral).sum()
 after=np.abs(np.array(Image.open(O/'frozen.png')).astype(float)-neutral).sum()
 assert after<before*.4,(before,after)
 open_recipe(base);checked(op='portrait_state',panel=8,click='portraitRepairBrush',brushSize=30)
 p=[face[0]+face[2]*.3,face[1]+face[3]*.65];q=[p[0]+face[2]*.1,p[1]]
 checked(op='portrait_state',paint=q,alt=True);r=checked(op='portrait_state',paint=p)
 assert not render('repair')['identity']
 key='repair.item.'+r['recipe']['people'][0]['settings']['repairs'][0]['id']
 checked(op='portrait_state',rangeKey=key,rangeMode=2,brushSize=160,paint=p)
 assert render('repair-erased')['identity']
 checked(op='portrait_state',history=-1);assert not render('repair-restored')['identity']
 open_recipe(base);checked(op='portrait_state',panel=12,parameter='hair.accessory',value=90)
 render('accessory')
 accessory=[face[0]+face[2]*.85,face[1]-face[3]*.15]
 checked(op='portrait_state',rangeKey='hair.accessory',rangeMode=2,brushSize=180,paint=accessory)
 render('accessory-erased')
 checked(op='portrait_state',parameter='hair.accessoryX',value=-80)
 assert not render('accessory-moved')['identity']
 # The collarbone material can be positioned on exposed neck skin; clothing stays excluded.
 body=copy.deepcopy(base);body['people'][0]['settings']['bodyMaterial']='collarbone'
 open_recipe(body);checked(op='portrait_state',panel=13,parameter='contour.lines',value=70)
 checked(op='portrait_state',parameter='contour.linesY',value=-15)
 r=render('collarbone');assert not r['identity']
 assert r['regionError']<2e-6
 checked(op='portrait_close');checked(op='window')
 sync=[]
 for accept in [False,True]:
  r=checked(op='work_window_probe',kind='portraitSession',source=str(source),recipe=base,accept=accept,syncSource=str(R/'research/m2/instances/FudanPed00001.png'))
  print('sync',accept,r,flush=True)
  assert not r.get('timeout') and r.get('stagedTargets')==1,r
  assert r['newDocuments']==int(accept) and r['revisionDelta']==int(accept),r
  if accept:assert r['syncedParameters']['skin.smooth']==25 and r['oneUndoRestores']
  sync.append(r)
 checked(op='shutdown')
 (O/'results.json').write_text(json.dumps(dict(passed=True,sync=sync),indent=2))
 print('M3 integrated controls and cross-document acceptance PASS',flush=True)
