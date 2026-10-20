from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import base64,io,copy
OUT=ROOT/'docs/test-evidence/M3/semantic-chest';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
name='christina-koch';source=ROOT/'docs/test-evidence/M3/quality'/f'{name}-source.png';size=np.array(Image.open(source).size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-shoulder'/f'{name}-production-recipe.json').read_text());p=recipe['people'][0]
path=[[.27,.49],[.25,.56],[.25,.62]]
p['settings']['chestContourPath']=path
p['settings'].pop('chestContourPath',None);p['settings']['parameters']={}
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);state=call(op='portrait_state')
  if state['ready'] and not state['previewBusy'] and state['displayedRevision']==state['previewRevision']:return state
 raise AssertionError(state['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=3,select=0);wait()
call(op='portrait_state',click='portraitChestLocate')
call(op='portrait_state',paint=path[0]);call(op='portrait_state',paint=path[1])
call(op='portrait_state',capture=str(OUT/'placement-two-points.png'))
call(op='portrait_state',paint=path[1],rightClick=True)
assert 'chestContourPaths' not in wait()['recipe']['people'][0]['settings']
for point in path[1:]:call(op='portrait_state',paint=point)
located=wait();settings=located['recipe']['people'][0]['settings'];key='left' if settings['side']==0 else 'right';assert len(settings['chestContourPaths'][key])==3,located['status']
call(op='portrait_state',parameter='body.chest',value=65);adjusted=wait();assert adjusted['recipe']['people'][0]['settings']['parameters']['body.chest.'+key]==65,adjusted['status']
call(op='portrait_state',history=-1);assert wait()['recipe']==located['recipe']
call(op='portrait_state',history=1);assert wait()['recipe']==adjusted['recipe']
call(op='portrait_state',doubleClickParameter='body.chest');assert wait()['recipe']['people'][0]['settings']['parameters'].get('body.chest.'+key,0)==0
call(op='portrait_state',history=-1);wait()
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'chest.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
saved=wait()['recipe'];call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=saved);wait();call(op='portrait_state',source=str(source),render=str(OUT/'workflow-reopened.png'))
assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'workflow-reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Three-point chest placement, adjustment, reset, history and saved rendering passed.')
