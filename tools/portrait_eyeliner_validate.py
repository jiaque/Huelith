from pathlib import Path
import base64,io,copy,math
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-eyeliner';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
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

p['settings']['eyeLinerVersion']=2
styles=[]
for style in ['soft','round','wing']:
 p['settings']['linerMaterial']=style;styles.append(mask('makeup.liner.right',style))
assert all(m.max()>100 for m in styles)
assert not np.array_equal(styles[0],styles[1]) and not np.array_equal(styles[1],styles[2])
assert styles[2][:,:60].max()>0 and styles[0][:,:60].max()==0, 'wing extends outer corner only'
lower=mask('makeup.linerLower.right','lower');assert lower[140:].max()>0 and lower[:128].max()==0
eye_region=Image.new('L',(400,240));ImageDraw.Draw(eye_region).polygon([(60,140),(80,128),(120,128),(140,140),(120,152),(80,152)],fill=255)
from PIL import ImageFilter
inside=np.asarray(eye_region.filter(ImageFilter.MinFilter(3)))>0
assert all(m[inside].max()==0 for m in styles+[lower])
for obstruction in ['hair','eyeglassFrame']:
 p['settings']['automaticMasks'][obstruction]=png(Image.new('L',(400,240),255));assert mask('makeup.liner.right',obstruction).max()==0;p['settings']['automaticMasks'].pop(obstruction)
recipe=real;p=recipe['people'][0];s=p['settings'];s['eyeLinerVersion']=2;s['parameters']={};source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';frames=[]
for name in ['original','soft','round','wing']:
 s['linerMaterial']=name;s['parameters']={} if name=='original' else {'makeup.liner.left':70,'makeup.liner.right':70,'makeup.linerLower.left':25,'makeup.linerLower.right':25}
 path=OUT/(name+'-real.png');call(op='portrait_geometry',recipe=recipe,source=str(source),size=list(Image.open(source).size),render=str(path));im=Image.open(path).convert('RGB');x,y,w,h=p['face'];frames.append(im.crop((int(x*im.width),int(y*im.height),int((x+w)*im.width),int((y+h*.65)*im.height))))
board=Image.new('RGB',(frames[0].width*2,frames[0].height*2))
for i,im in enumerate(frames):board.paste(im,((i%2)*im.width,(i//2)*im.height))
board.resize((board.width*3,board.height*3)).save(OUT/'compare.png')

s['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=2);before=wait()['recipe'];call(op='portrait_state',click='portraitMaterialCard_linerMaterial_round');after=wait()['recipe'];assert after['people'][0]['settings']['eyeLinerVersion']==2
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
call(op='portrait_state',parameter='makeup.linerLower',value=35);after=wait()['recipe']
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'liner.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Liner profiles, lower lid, wing orientation, eye protection, UI and saving passed')
