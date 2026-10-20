from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,cv2
OUT=ROOT/'docs/test-evidence/M3/semantic-lip-extras';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());recipe=copy.deepcopy(real);p=recipe['people'][0];s=p['settings'];s['parameters']={'makeup.lip':80};s['lipMaterialVersion']=2;s['mouthTopologyVersion']=2;s['automaticMasks']={};p['landmarks']=[[.1,.1,1] for _ in range(66)]
outer=[58,48,49,50,51,52,62,53,54,55,56,57];inner=[58,59,60,61,62,63,64,65]
outer_xy=[(80,120),(90,100),(115,85),(140,90),(165,85),(190,100),(200,120),(190,140),(165,150),(140,155),(115,150),(90,140)]
inner_xy=[(80,120),(110,110),(140,108),(170,110),(200,120),(170,135),(140,138),(110,135)]
for indices,coords in [(outer,outer_xy),(inner,inner_xy)]:
 for i,(x,y) in zip(indices,coords):p['landmarks'][i]=[x/280,y/240,1]
def wait():
 end=time.monotonic()+45
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])

import base64,io
s['parameters']={}
def mask(key,name):
 path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=[280,240],styleMask=key,mask=str(path));return np.asarray(Image.open(path))
s['parameters']['makeup.lipBleedWidth']=20;a=mask('makeup.lipBleed','narrow');s['parameters']['makeup.lipBleedWidth']=100;b=mask('makeup.lipBleed','wide');assert b.sum()>a.sum()>0
inside=np.array([[cv2.pointPolygonTest(np.array(outer_xy,np.float32),(x+.5,y+.5),False)>=0 for x in range(280)] for y in range(240)]);assert b[inside].max()==0
far=np.array([[cv2.pointPolygonTest(np.array(outer_xy,np.float32),(x+.5,y+.5),True)<-4.1 for x in range(280)] for y in range(240)]);assert b[far].max()==0
s['parameters']['makeup.lipSparkleDensity']=-50;s['parameters']['makeup.lipSparkleSize']=30;a=mask('makeup.lipSparkle','sparse');s['parameters']['makeup.lipSparkleDensity']=70;b=mask('makeup.lipSparkle','dense');assert b.sum()>a.sum()>0
assert np.array_equal(b,mask('makeup.lipSparkle','repeat'))
s['parameters']['makeup.lipSparkleSize']=80;c=mask('makeup.lipSparkle','larger');assert c.sum()>b.sum()
oral=np.array([[cv2.pointPolygonTest(np.array(inner_xy,np.float32),(x+.5,y+.5),False)>=0 for x in range(280)] for y in range(240)]);assert c[oral].max()==0 and c[~inside].max()==0
high=OUT/'double-resolution.png';call(op='portrait_geometry',recipe=recipe,size=[560,480],styleMask='makeup.lipSparkle',mask=str(high));down=cv2.resize(np.asarray(Image.open(high)).astype(np.float32),(280,240),interpolation=cv2.INTER_AREA)
error=np.mean(abs(down-c));assert all(error<np.mean(abs(np.roll(down,shift,axis)-c)) for shift,axis in [(1,0),(-1,0),(1,1),(-1,1)]), 'changing preview resolution must not move the particle pattern'
recipe=real;p=recipe['people'][0];s=p['settings'];source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';frames=[]
for name,params in [('original',{}),('bleed',{'makeup.lipBleed':100,'makeup.lipBleedWidth':100}),('sparkle',{'makeup.lipSparkle':100,'makeup.lipSparkleDensity':20,'makeup.lipSparkleSize':30})]:
 s['parameters']=params;path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,source=str(source),size=list(Image.open(source).size),render=str(path));im=Image.open(path).convert('RGB');x,y,w,h=p['face'];frames.append(im.crop((int(x*im.width),int((y+h*.5)*im.height),int((x+w)*im.width),int((y+h)*im.height))))
board=Image.new('RGB',(frames[0].width*3,frames[0].height))
for i,im in enumerate(frames):board.paste(im,(i*im.width,0))
board.resize((board.width*3,board.height*3)).save(OUT/'compare.png')
s['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=0);before=wait()['recipe'];call(op='portrait_state',parameter='makeup.lipBleed',value=65);after=wait()['recipe']
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
call(op='portrait_state',parameter='makeup.lipSparkle',value=70);wait();call(op='portrait_state',parameter='makeup.lipSparkleSize',value=40);wait();call(op='portrait_state',parameter='makeup.lipSparkleDensity',value=35);after=wait()['recipe'];assert after['people'][0]['settings']['parameters']['makeup.lipBleed']==65
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'extras.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
portable=call(op='portrait_geometry',portableSkin=after['people'][0]['settings'],makeupPreset=True,targetSettings={})['portable'];assert portable['parameters']['makeup.lipSparkleDensity']==35
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close')
# Erasing coverage removes only the selected material.
black=io.BytesIO();Image.new('L',(32,32),0).save(black,format='PNG');masked=after;settings=masked['people'][0]['settings'];settings['parameters']={'makeup.lipBleed':100,'makeup.lipBleedWidth':100};settings['masks']={'makeup.lipBleed':base64.b64encode(black.getvalue()).decode()};call(op='portrait_geometry',recipe=masked,source=str(source),size=list(Image.open(source).size),render=str(OUT/'erased.png'));assert np.array_equal(np.asarray(Image.open(OUT/'erased.png')),np.asarray(Image.open(OUT/'original.png')))
call(op='shutdown');print('Bounded outer bleed, stable UV particles, independent size/density, erase, undo and persistence passed')
