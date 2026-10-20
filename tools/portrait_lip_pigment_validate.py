from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,cv2
OUT=ROOT/'docs/test-evidence/M3/semantic-lip-pigment';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());recipe=copy.deepcopy(real);p=recipe['people'][0];s=p['settings'];s['parameters']={'makeup.lip':80};s['lipMaterialVersion']=2;s['mouthTopologyVersion']=2;s['automaticMasks']={};p['landmarks']=[[.1,.1,1] for _ in range(66)]
outer=[58,48,49,50,51,52,62,53,54,55,56,57];inner=[58,59,60,61,62,63,64,65]
outer_xy=[(80,120),(90,100),(115,85),(140,90),(165,85),(190,100),(200,120),(190,140),(165,150),(140,155),(115,150),(90,140)]
inner_xy=[(80,120),(110,110),(140,108),(170,110),(200,120),(170,135),(140,138),(110,135)]
for indices,coords in [(outer,outer_xy),(inner,inner_xy)]:
 for i,(x,y) in zip(indices,coords):p['landmarks'][i]=[x/280,y/240,1]
def mask(style,feather):
 s['lipMaterial']=style;s['parameters']['makeup.lipFeather']=feather;path=OUT/f'{style}-{feather}.png';call(op='portrait_geometry',recipe=recipe,size=[280,240],styleMask='makeup.lip',mask=str(path));return np.asarray(Image.open(path))
a=mask('sheer',0);b=mask('bitten',0);c=mask('sheer',100);assert b.sum()<a.sum() and b[104,140]>b[94,140] and c.sum()<a.sum()
inside=np.zeros((240,280),np.uint8);cv2.fillPoly(inside,[np.array(inner_xy)],255);outside=np.zeros_like(inside);cv2.fillPoly(outside,[np.array(outer_xy)],255)
outside_centers=np.array([[cv2.pointPolygonTest(np.array(outer_xy,np.float32),(x+.5,y+.5),False)<=0 for x in range(280)] for y in range(240)])
for m in [a,b,c]:assert m[cv2.erode(inside,np.ones((3,3),np.uint8))>0].max()==0 and m[outside_centers].max()==0
source=OUT/'striped.png';arr=np.full((240,280,3),(185,125,115),np.uint8);arr[:,::3]=[170,110,100];arr[inside>0]=[240,240,235];Image.fromarray(arr).save(source)
for name,coverage in [('sheer',0),('opaque',75)]:
 s['lipMaterial']=name;s['parameters']['makeup.lipCoverage']=coverage;s['parameters']['makeup.lipFeather']=10;call(op='portrait_geometry',recipe=recipe,source=str(source),size=[280,240],render=str(OUT/(name+'.png')))
a=np.asarray(Image.open(OUT/'sheer.png'));b=np.asarray(Image.open(OUT/'opaque.png'));assert not np.array_equal(a,b);assert np.array_equal(a[cv2.erode(inside,np.ones((3,3),np.uint8))>0],b[cv2.erode(inside,np.ones((3,3),np.uint8))>0]);assert np.std(b[145,120:160,0])>1
# Actual smiling portrait, with texture and teeth retained.
recipe=real;p=recipe['people'][0];s=p['settings'];s['lipMaterialVersion']=2;s['mouthTopologyVersion']=2;source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';frames=[]
for style in ['original','sheer','opaque','bitten']:
 s['lipMaterial']=style;s['parameters']={} if style=='original' else {'makeup.lip':80,'makeup.lipCoverage':75 if style=='opaque' else 20 if style=='bitten' else 30,'makeup.lipFeather':25 if style=='bitten' else 10};dest=OUT/(style+'-real.png');call(op='portrait_geometry',recipe=recipe,source=str(source),size=list(Image.open(source).size),render=str(dest));im=Image.open(dest).convert('RGB');x,y,w,h=p['face'];frames.append(im.crop((int(x*im.width),int((y+h*.5)*im.height),int((x+w)*im.width),int((y+h)*im.height))))
board=Image.new('RGB',(frames[0].width*2,frames[0].height*2))
for i,im in enumerate(frames):board.paste(im,((i%2)*im.width,(i//2)*im.height))
board.resize((board.width*3,board.height*3)).save(OUT/'compare.png')
def wait():
 end=time.monotonic()+45
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
s['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=0);before=wait()['recipe'];call(op='portrait_state',click='portraitMaterialCard_lipMaterial_opaque');after=wait()['recipe'];assert after['people'][0]['settings']['parameters']['makeup.lipCoverage']==75
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
call(op='portrait_state',click='portraitSwatch_be4c5b');after=wait()['recipe'];assert after['people'][0]['settings']['lipMaterial']=='opaque'
portable=call(op='portrait_geometry',portableSkin=after['people'][0]['settings'],makeupPreset=True,targetSettings={});assert portable['merged']['lipMaterial']=='opaque' and portable['merged']['lipMaterialVersion']==2
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'lip.huelith'),capture=str(OUT/'ui.png'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Lip distributions, feather boundaries, oral protection, coverage texture, UI selection, undo and saving passed')
