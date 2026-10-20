from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-nails';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
source=ROOT/'build/nail-evaluation/hand-1.jpg';size=np.array(Image.open(source).size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text(encoding='utf-8'));person=recipe['people'][0];person['settings']={'partialSkin':True,'parameters':{}};person['landmarks']=[];person['face']=[0,0,1,1];person.pop('bodyMask',None)
call(op='portrait_window',path=str(source),recipe=recipe);recipe['sourceHash']=wait()['recipe']['sourceHash'];call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=10);before=wait()['recipe']
# Test actual freehand UI using a known contour; this does not claim product inference.
cases=json.loads((ROOT/'docs/test-evidence/M3/nail-model-evaluation/results.json').read_text(encoding='utf-8'))['cases'];polygon=np.array(cases[0]['instances'][0]['polygon'])/size;points=np.vstack([polygon,polygon[0]]).tolist()
call(op='portrait_state',click='portraitNailOutline');call(op='portrait_state',paintPath=points);added=wait()['recipe'];nails=added['people'][0]['settings'].get('nails',[]);assert len(nails)==1,{'nailCount':len(nails)}
call(op='portrait_state',history=-1);undone=wait()['recipe'];(OUT/'undo-before.json').write_text(json.dumps(before),encoding='utf-8');(OUT/'undo-after.json').write_text(json.dumps(undone),encoding='utf-8');assert undone==before;call(op='portrait_state',history=1);wait();call(op='portrait_state',click='portraitNailColor_416179');colored=wait()['recipe'];assert colored['people'][0]['settings']['nails'][0]['color']!=nails[0]['color']
(OUT/'colored-recipe.json').write_text(json.dumps(colored),encoding='utf-8')
r=call(op='portrait_state',source=str(source),render=str(OUT/'colored.png'),save=str(OUT/'nails.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,{'roundtrip':r['roundtrip'],'regionError':r['regionError']}
matte=copy.deepcopy(colored);matte['people'][0]['settings']['parameters']['makeup.nailsGloss']=0
call(op='portrait_geometry',recipe=matte,source=str(source),styleOnly=str(OUT/'matte.png'))
# Geometry mask and render coverage outside exact contour must be untouched.
call(op='portrait_geometry',recipe=colored,size=size.tolist(),styleMask='makeup.nails',mask=str(OUT/'mask.png'))
base=copy.deepcopy(colored);base['people'][0]['settings']['parameters']['makeup.nails']=0
call(op='portrait_geometry',recipe=base,source=str(source),styleOnly=str(OUT/'baseline.png'))
call(op='portrait_geometry',recipe=colored,source=str(source),styleOnly=str(OUT/'style.png'))
a=np.array(Image.open(OUT/'baseline.png')).astype(float);b=np.array(Image.open(OUT/'style.png')).astype(float);mask=np.array(Image.open(OUT/'mask.png'));diff=np.max(abs(a-b),axis=2);assert diff[mask==0].max()==0;assert diff[mask>128].mean()>5;assert np.max(abs(b-np.array(Image.open(OUT/'matte.png'))))>5
call(op='portrait_state',click='portraitNailRemove');assert len(wait()['recipe']['people'][0]['settings']['nails'])==0
call(op='portrait_state',history=-1);assert wait()['recipe']==colored
call(op='portrait_state',click='portraitNailOutline');call(op='portrait_state',paintPath=points,paintEnd='ungrab');assert wait()['recipe']==colored
call(op='portrait_state',click='portraitNailRedraw');call(op='portrait_state',paintPath=(np.array(points)+[.001,.002]).tolist());redrawn=wait()['recipe'];assert len(redrawn['people'][0]['settings']['nails'])==1 and redrawn['people'][0]['settings']['nails'][0]['color']==colored['people'][0]['settings']['nails'][0]['color'];call(op='portrait_state',history=-1);assert wait()['recipe']==colored
call(op='portrait_state',click='portraitNailOutline')
# A second nail is independently stored and colored.
poly2=np.array(cases[0]['instances'][1]['polygon'])/size;call(op='portrait_state',paintPath=np.vstack([poly2,poly2[0]]).tolist());two=wait()['recipe'];assert len(two['people'][0]['settings']['nails'])==2;assert two['people'][0]['settings']['nails'][0]==colored['people'][0]['settings']['nails'][0]
call(op='portrait_state',click='portraitNailErase');center=polygon.mean(axis=0).tolist();call(op='portrait_state',brushSize=3,paintPath=[center,(np.array(center)+[.005,0]).tolist()]);erased=wait()['recipe'];assert 'makeup.nails' in erased['people'][0]['settings']['masks'];call(op='portrait_state',history=-1);assert wait()['recipe']==two
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=colored);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'colored.png')),np.array(Image.open(OUT/'reopened.png')))
portable=call(op='portrait_geometry',portableSkin=colored['people'][0]['settings'],makeupPreset=True,targetSettings={})['merged'];assert 'nails' not in portable
call(op='portrait_close');call(op='shutdown');print('Manual nail contours, individual pigment, outside coverage, undo, cancelled gesture, erase, ROI and reopen passed')
