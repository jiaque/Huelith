from pathlib import Path
import base64,io,copy,math
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-aegyosal';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());real=copy.deepcopy(recipe);p=recipe['people'][0];p['face']=[0,0,1,1];p['settings']['parameters']={};p['settings']['eyeShadowVersion']=2;p['landmarks']=[[.05,.05,1] for _ in range(66)]
def png(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
p['settings']['automaticMasks']={'skin':png(Image.new('L',(400,240),255))}
for eye,brow,cx in [(36,17,100),(42,22,300)]:
 for i,(dx,dy) in enumerate([(-40,0),(-20,-12),(20,-12),(40,0),(20,12),(-20,12)]):p['landmarks'][eye+i]=[(cx+dx)/400,(140+dy)/240,1]
 for i,(dx,dy) in enumerate([(-48,-40),(-24,-49),(0,-52),(24,-49),(48,-40)]):p['landmarks'][brow+i]=[(cx+dx)/400,(140+dy)/240,1]
def mask(key,name):
 path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=[400,240],styleMask=key,mask=str(path));return np.asarray(Image.open(path))
def wait():
 end=time.monotonic()+45
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])

a=mask('makeup.aegyosal.right','field');b=mask('makeup.aegyosalShade.right','shade');assert a.max()>150 and b.max()>50
assert a[:150,80:120].max()==0 and a[180:].max()==0
assert np.all(b<=a), 'shadow is contained in the field'
for obstruction in ['hair','eyeglassFrame']:
 p['settings']['automaticMasks'][obstruction]=png(Image.new('L',(400,240),255));assert mask('makeup.aegyosal.right',obstruction).max()==0;p['settings']['automaticMasks'].pop(obstruction)
source=OUT/'flat.png';Image.new('RGB',(400,240),(150,120,100)).save(source)
s=p['settings'];s['parameters']={};call(op='portrait_geometry',recipe=recipe,source=str(source),size=[400,240],render=str(OUT/'flat-before.png'))
s['parameters']={'makeup.aegyosal.right':100};call(op='portrait_geometry',recipe=recipe,source=str(source),size=[400,240],render=str(OUT/'flat-after.png'))
delta=np.asarray(Image.open(OUT/'flat-after.png')).astype(int)-np.asarray(Image.open(OUT/'flat-before.png')).astype(int)
assert delta[:,:,:3].max()>3 and delta[:,:,:3].min()<-3, 'both highlight and lower shadow are required'
assert np.max(np.abs(delta[:120]))==0 and np.max(np.abs(delta[190:]))==0
recipe=real;p=recipe['people'][0];s=p['settings'];source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';frames=[]
for name,params in [('original',{}),('volume',{'makeup.aegyosal.left':70,'makeup.aegyosal.right':70})]:
 s['parameters']=params;path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,source=str(source),size=list(Image.open(source).size),render=str(path));im=Image.open(path).convert('RGB');x,y,w,h=p['face'];frames.append(im.crop((int(x*im.width),int(y*im.height),int((x+w)*im.width),int((y+h*.75)*im.height))))
board=Image.new('RGB',(frames[0].width*2,frames[0].height))
for i,im in enumerate(frames):board.paste(im,(i*im.width,0))
board.resize((board.width*3,board.height*3)).save(OUT/'compare.png')
s['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=2);before=wait()['recipe'];call(op='portrait_state',parameter='makeup.aegyosal',value=65);after=wait()['recipe'];assert 'eye.bags' not in after['people'][0]['settings']['parameters']
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'volume.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Lower-lid narrow highlight and shadow, protections, independent control, undo and saving passed')
