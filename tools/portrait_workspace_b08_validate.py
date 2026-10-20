from pathlib import Path
import copy
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b08';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/john-glenn-source.png';size=np.array(Image.open(source).size);recipe=json.loads((source.parent/'john-glenn-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
path=[[x/943,(249-4*(x-335)/70)/1200] for x in range(335,406,5)]
def render(name):
 call(op='portrait_state',source=str(source),render=str(OUT/(name+'.png')))
 return np.array(Image.open(OUT/(name+'.png')).convert('RGB')).astype(float)
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=6,select=0,brushSize=22);before=wait();original=render('original')
 call(op='portrait_state',paintPath=path,paintEnd='ungrab');state=wait();strokes=state['recipe']['people'][0]['settings']['repairs'];assert len(strokes)==1 and strokes[0]['kind']=='wrinkle' and strokes[0].get('wrinkleMethod')!='lighting-v2' and len(strokes[0]['points'])>3;assert state['history']==before['history']+1
 texture=render('texture');assert abs(texture-original).max()>0
 call(op='portrait_state',hover=path[-1],capture=str(OUT/'046.png'));saved=state['recipe'];call(op='portrait_state',history=-1);assert wait()['recipe']==before['recipe'];call(op='portrait_state',history=1);assert wait()['recipe']==saved
 # New source is for the next stroke, never rewriting previous work.
 call(op='portrait_state',paint=[path[0][0],path[0][1]-.025],alt=True);assert wait()['recipe']==saved
 call(op='portrait_state',wrinkleMode=1);before=wait();call(op='portrait_state',paint=[.5,.2],alt=True);assert wait()['recipe']==before['recipe']
 call(op='portrait_state',paintPath=path);state=wait();strokes=state['recipe']['people'][0]['settings']['repairs'];assert len(strokes)==2 and strokes[1]['wrinkleMethod']=='lighting-v2';assert state['history']==before['history']+1
 both=render('both');assert abs(both-texture).max()>0;call(op='portrait_state',capture=str(OUT/'047.png'))
 call(op='portrait_state',wrinkleMode=0,click='portraitWrinkleTypeErase',brushSize=60);before=wait();call(op='portrait_state',paintPath=path);erased=wait();masks=erased['recipe']['people'][0]['settings']['masks'];assert 'repair.item.'+strokes[0]['id'] in masks and 'repair.item.'+strokes[1]['id'] not in masks;assert erased['history']==before['history']+1
 eraseimage=render('texture-erased');assert abs(eraseimage-both).max()>0;call(op='portrait_state',capture=str(OUT/'048.png'));call(op='portrait_state',history=-1);assert wait()['recipe']==before['recipe'];call(op='portrait_state',history=1);assert wait()['recipe']==erased['recipe']
 call(op='portrait_state',click='portraitWrinklePaint',brushSize=22,paintPath=path);state=wait();newstroke=state['recipe']['people'][0]['settings']['repairs'][-1];assert 'repair.item.'+newstroke['id'] not in state['recipe']['people'][0]['settings']['masks'];assert abs(render('repainted')-eraseimage).max()>0
 call(op='portrait_state',wrinkleMode=1,click='portraitWrinkleTypeErase',brushSize=60,paintPath=path);state=wait();assert 'repair.item.'+strokes[1]['id'] in state['recipe']['people'][0]['settings']['masks'];assert state['recipe']['people'][0]['settings']['masks']['repair.item.'+strokes[0]['id']]==masks['repair.item.'+strokes[0]['id']]
 # Short eye-corner and longer nasolabial strokes, both algorithms, preserve prior work.
 extra=[[[x/943,y/1200] for x,y in [(305,311),(300,315),(295,319)]],[[x/943,y/1200] for x,y in [(413,341),(420,346),(425,353),(429,361)]]]
 for mode in [0,1]:
  call(op='portrait_state',wrinkleMode=mode,click='portraitWrinklePaint',brushSize=10)
  for points in extra:
   before=wait();beforePixels=render('detail-before');call(op='portrait_state',paintPath=points);state=wait();afterPixels=render('detail-after');assert abs(afterPixels-beforePixels).max()>0,(mode,points,state['status']);assert len(state['recipe']['people'][0]['settings']['repairs'])==len(before['recipe']['people'][0]['settings']['repairs'])+1,state['status']
 r=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'wrinkle.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',project=str(OUT/'wrinkle.huelith'));assert wait()['recipe']==saved;assert np.array_equal(render('reopened'),np.array(Image.open(OUT/'saved.png').convert('RGB')).astype(float))
 # Cross-entry navigation must leave the accumulated recipe intact.
 for task in [3,9,4,5,10,6]:call(op='portrait_state',workspaceTask=task);wait()
 assert wait()['recipe']==saved
 crop=(285,225,445,330)
 # Separate clear comparison, all four same-size crops.
 sheet=Image.new('RGB',(640,420))
 for i,name in enumerate(['original','texture','both','texture-erased']):sheet.paste(Image.open(OUT/(name+'.png')).crop(crop).resize((320,210)),((i%2)*320,(i//2)*210))
 sheet.save(OUT/'comparison.png');(OUT/'result.json').write_text(json.dumps({'textureAndLighting':True,'typeEraseIsolation':True,'repaintAfterErase':True,'altDoesNotChangeLighting':True,'continuousAndShortStrokes':True,'undoSaveReopen':True,'crossEntry':True},indent=2));print('B08 group passed')
finally:call(op='portrait_close');call(op='shutdown')
