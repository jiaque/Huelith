from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import base64,io,copy
OUT=ROOT/'docs/test-evidence/M3/semantic-chest';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
source=ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg';size=np.array(Image.open(source).size)
recipe=json.loads((source.parent/'mediapipe-pose-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
paths=[[[.456,.522],[.461,.545],[.461,.57]],[[.538,.522],[.536,.545],[.534,.57]]]
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);state=call(op='portrait_state')
  if state['ready'] and not state['previewBusy'] and state['displayedRevision']==state['previewRevision']:return state
 raise AssertionError(state['status'])

def settings():return wait()['recipe']['people'][0]['settings']
def locate(path):
 call(op='portrait_state',click='portraitChestLocate')
 for point in path:call(op='portrait_state',paint=point)
 state=settings();return 'left' if state['side']==0 else 'right'
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=3,select=0);wait()
first=locate(paths[0]);call(op='portrait_state',parameter='body.chest',value=65);firstState=settings();firstPath=firstState['chestContourPaths'][first]
# Linking while the second contour is absent must not corrupt the first.
call(op='portrait_state',linked=True);before=wait()['recipe'];call(op='portrait_state',parameter='body.chest',value=25);assert wait()['recipe']==before
second=locate(paths[1]);assert second!=first
state=settings();assert state['chestContourPaths'][first]==firstPath and state['parameters']['body.chest.'+first]==65
call(op='portrait_state',parameter='body.chest',value=-40);state=settings();assert state['parameters']['body.chest.'+second]==-40
for key,value in [(first,80),(second,-20),(first,45)]:
 call(op='portrait_state',click='portraitSideChoice'+('1' if key=='left' else '2'));wait()
 other=second if key==first else first;old=settings()['parameters']['body.chest.'+other]
 call(op='portrait_state',parameter='body.chest',value=value);state=settings();assert state['parameters']['body.chest.'+key]==value and state['parameters']['body.chest.'+other]==old
before=wait()['recipe'];call(op='portrait_state',doubleClickParameter='body.chest');state=settings();assert state['parameters']['body.chest.'+first]==0 and state['parameters']['body.chest.'+second]==-20
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',linked=True,parameter='body.chest',value=30);state=settings();assert state['parameters']['body.chest.left']==state['parameters']['body.chest.right']==30
# Relocate a contour after face shaping and manual liquify: clicks are in the
# displayed geometry but the stored contour must stay in source coordinates.
call(op='portrait_state',panel=0,parameter='face.width',value=-15);wait()
call(op='portrait_state',liquify=True);wait()
result=call(op='portrait_geometry',size=size.tolist(),recipe=wait()['recipe'],field=str(OUT/'combined-field.bin'))
w,h=result['meshSize'];field=np.fromfile(OUT/'combined-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
displayed=forward_points(field,np.array(paths[0]))
call(op='portrait_state',panel=3);wait();relocated=locate(displayed.tolist())
assert relocated==first
stored=np.array(settings()['chestContourPaths'][first])
assert np.linalg.norm((stored-np.array(paths[0]))*(size-1),axis=1).max()<2
assert settings()['parameters']['body.chest.'+second]==30
saved=wait()['recipe'];r=call(op='portrait_state',source=str(source),render=str(OUT/'bilateral.png'),save=str(OUT/'bilateral.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=saved);wait();call(op='portrait_state',source=str(source),render=str(OUT/'bilateral-reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'bilateral.png')),np.array(Image.open(OUT/'bilateral-reopened.png')))
call(op='portrait_close')
# An existing single-contour project must remain editable without relocalizing.
legacy=copy.deepcopy(recipe);s=legacy['people'][0]['settings']
s['chestContourPath']=paths[0];s.pop('chestContourPaths',None)
s['parameters']={'body.chest':65};s['side']=0 if first=='left' else 1;s['linked']=False
call(op='portrait_window',path=str(source),recipe=legacy);wait();call(op='portrait_state',panel=3,select=0);wait()
assert call(op='portrait_state',control='portraitParam_body.chest')['control']['value']==65
call(op='portrait_state',linked=True);before=wait()['recipe']
call(op='portrait_state',parameter='body.chest',value=90);assert wait()['recipe']==before
assert call(op='portrait_state',control='portraitParam_body.chest')['control']['value']==65
call(op='portrait_state',linked=False);wait()
call(op='portrait_state',parameter='body.chest',value=80);s=settings()
assert s['parameters']['body.chest.'+first]==80 and 'body.chest' not in s['parameters']
assert s['chestContourPaths'][first]==paths[0]
call(op='portrait_state',source=str(source),render=str(OUT/'legacy-edited.png'))
call(op='portrait_state',history=-1);assert settings()['parameters']['body.chest']==65
call(op='portrait_state',history=1);assert settings()['parameters']['body.chest.'+first]==80
call(op='portrait_state',click='portraitSideChoice'+('1' if second=='left' else '2'));before=wait()['recipe']
call(op='portrait_state',parameter='body.chest',value=20);assert wait()['recipe']==before
# Portable presets carry amounts, never source-person contour coordinates.
portable=call(op='portrait_geometry',portableSkin=state,bodyPreset=True,targetSettings=recipe['people'][0]['settings'])
assert 'chestContourPaths' not in portable['portable']
assert portable['portable']['parameters']['body.chest.left']==30
missing=copy.deepcopy(recipe);missing['people'][0]['settings']=portable['merged']
try:
 call(op='portrait_geometry',size=size.tolist(),recipe=missing,field=str(OUT/'missing-contour.bin'))
except Exception as error:
 assert '胸部' in str(error),str(error)
else:raise AssertionError('A preset without target contours silently succeeded')
call(op='portrait_close')
single=copy.deepcopy(legacy);s=single['people'][0]['settings']
s['chestContour']=s.pop('chestContourPath')[1]
call(op='portrait_window',path=str(source),recipe=single);wait();call(op='portrait_state',panel=3,select=0);wait()
call(op='portrait_state',doubleClickParameter='body.chest');s=settings()
assert s['parameters'].get('body.chest',0)==0
call(op='portrait_close');call(op='shutdown');print('Bilateral edits, legacy migration/reset, combined geometry, missing-contour preset rejection and save/reopen passed.')
