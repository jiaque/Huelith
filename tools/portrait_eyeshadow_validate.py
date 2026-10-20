from pathlib import Path
import base64,io,copy,math
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-eyeshadow';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());real=copy.deepcopy(recipe);p=recipe['people'][0];p['face']=[0,0,1,1];p['settings']['parameters']={};p['settings']['eyeShadowVersion']=2;p['landmarks']=[[.05,.05,1] for _ in range(66)]
def png(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
p['settings']['automaticMasks']={'skin':png(Image.new('L',(400,240),255))}
for eye,brow,cx in [(36,17,100),(42,22,300)]:
 for i,(dx,dy) in enumerate([(-40,0),(-20,-12),(20,-12),(40,0),(20,12),(-20,12)]):p['landmarks'][eye+i]=[(cx+dx)/400,(140+dy)/240,1]
 for i,(dx,dy) in enumerate([(-48,-40),(-24,-49),(0,-52),(24,-49),(48,-40)]):p['landmarks'][brow+i]=[(cx+dx)/400,(140+dy)/240,1]
def mask(key,name):
 path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=[400,240],styleMask=key,mask=str(path));return np.asarray(Image.open(path))
a=mask('makeup.shadow.right','base');b=mask('makeup.shadowAccent.right','accent');assert a.max()>200 and b.max()>150 and b.sum()<a.sum()
y,x=np.indices(a.shape);assert (x*b).sum()/b.sum()<(x*a).sum()/a.sum(), 'right outer corner should be image-left'
eye_region=Image.new('L',(400,240));ImageDraw.Draw(eye_region).polygon([(60,140),(80,128),(120,128),(140,140),(120,152),(80,152)],fill=255)
from PIL import ImageFilter
inside=np.asarray(eye_region.filter(ImageFilter.MinFilter(3)))>0
assert a[inside].max()==0 and a[:100].max()==0, 'eye and brow protected'
# Closed lid changes the lower boundary instead of retaining an open-eye ellipse.
for i in range(36,42):p['landmarks'][i][1]=140/240
closed=mask('makeup.shadow.right','closed');assert closed[130,100]>50 and a[130,100]==0
original=copy.deepcopy(p['landmarks']);angle=.35;c,s=math.cos(angle),math.sin(angle)
for i in list(range(36,42))+list(range(17,22)):
 xx=original[i][0]*400-100;yy=original[i][1]*240-140;p['landmarks'][i]=[(100+c*xx-s*yy)/400,(140+s*xx+c*yy)/240,1]
rotated=mask('makeup.shadow.right','rotated');assert rotated.max()>200
for obstruction in ['hair','eyeglassFrame']:
 p['settings']['automaticMasks'][obstruction]=png(Image.new('L',(400,240),255));assert mask('makeup.shadow.right',obstruction).max()==0;p['settings']['automaticMasks'].pop(obstruction)
# Real image comparison, then actual controls, undo, ROI and project roundtrip.
recipe=real;p=recipe['people'][0];s=p['settings'];s['eyeShadowVersion']=2;s['parameters']={};source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';frames=[]
for name,params in [('original',{}),('base',{'makeup.shadow.left':70,'makeup.shadow.right':70}),('two-tone',{'makeup.shadow.left':70,'makeup.shadow.right':70,'makeup.shadowAccent.left':65,'makeup.shadowAccent.right':65})]:
 s['parameters']=params;path=OUT/(name+'-real.png');call(op='portrait_geometry',recipe=recipe,source=str(source),size=list(Image.open(source).size),render=str(path));im=Image.open(path).convert('RGB');x,y,w,h=p['face'];frames.append(im.crop((int(x*im.width),int(y*im.height),int((x+w)*im.width),int((y+h*.65)*im.height))))
board=Image.new('RGB',(frames[0].width*3,frames[0].height))
for i,im in enumerate(frames):board.paste(im,(i*im.width,0))
board.resize((board.width*3,board.height*3)).save(OUT/'compare.png')
def wait():
 end=time.monotonic()+45
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
s['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=2);before=wait()['recipe'];call(op='portrait_state',parameter='makeup.shadow',value=55);wait();call(op='portrait_state',parameter='makeup.shadowAccent',value=40);after=wait()['recipe'];assert after['people'][0]['settings']['eyeShadowVersion']==2
call(op='portrait_state',history=-1);assert wait()['recipe']['people'][0]['settings']['parameters'].get('makeup.shadowAccent.left',0)==0
call(op='portrait_state',history=1);assert wait()['recipe']==after
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'shadow.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Upper-lid shadow, accent, closure, rotation, obstructions, UI undo and saving passed')
