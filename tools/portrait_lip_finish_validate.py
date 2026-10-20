from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,cv2
OUT=ROOT/'docs/test-evidence/M3/semantic-lip-finish';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
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
source=OUT/'highlight.png';im=Image.new('RGB',(280,240),(160,80,80));d=ImageDraw.Draw(im);d.ellipse((124,142,157,150),fill=(240,210,205));d.polygon(inner_xy,fill=(245,245,240));im.save(source)
s['parameters']={};s['lipFinishVersion']=2;s['lipLighting']=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True)['lipLighting'];assert s['lipLighting']
frames=[]
for value in [0,-100,100]:
 s['parameters']={'makeup.lipGloss':value};out=OUT/f'finish-{value}';call(op='portrait_geometry',recipe=recipe,source=str(source),size=[280,240],render=str(out.with_suffix('.png')),linearRender=str(out.with_suffix('.bin')));frames.append(np.fromfile(out.with_suffix('.bin'),np.float32).reshape(240,280,4))
a,b,c=frames;assert b[146,140,0]<a[146,140,0]-.01;assert np.max(c-a)>.005 and np.max(c[:,:,:3])<=1.00001
assert np.array_equal(a[:,:,3],b[:,:,3]) and np.array_equal(a[:,:,3],c[:,:,3]);assert np.max(abs(a[:80]-b[:80]))==0 and np.max(abs(a[:80]-c[:80]))==0
assert np.array_equal(a[120,140],b[120,140]) and np.array_equal(a[120,140],c[120,140]), 'teeth/oral region must remain unchanged'
flat=OUT/'flat.png';Image.new('RGB',(280,240),(160,80,80)).save(flat);lighting=call(op='portrait_geometry',source=str(flat),person=p,repairAnalysis=True)['lipLighting'];assert np.asarray(Image.open(io.BytesIO(base64.b64decode(lighting['matte'])))).max()==0, 'matte must not darken diffuse flat lips'
recipe=real;p=recipe['people'][0];s=p['settings'];source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';s['lipFinishVersion']=2;s['lipLighting']=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True)['lipLighting'];frames=[]
for name,value in [('original',0),('matte',-100),('gloss',100)]:
 s['parameters']={'makeup.lipGloss':value};path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,source=str(source),size=list(Image.open(source).size),render=str(path));im=Image.open(path).convert('RGB');x,y,w,h=p['face'];frames.append(im.crop((int(x*im.width),int((y+h*.5)*im.height),int((x+w)*im.width),int((y+h)*im.height))))
board=Image.new('RGB',(frames[0].width*3,frames[0].height))
for i,im in enumerate(frames):board.paste(im,(i*im.width,0))
board.resize((board.width*3,board.height*3)).save(OUT/'compare.png')
s['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=0);before=wait()['recipe'];call(op='portrait_state',parameter='makeup.lipGloss',value=-80);after=wait()['recipe'];assert after['people'][0]['settings']['parameters'].get('makeup.lip',0)==0
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'finish.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
portable=call(op='portrait_geometry',portableSkin=after['people'][0]['settings'],makeupPreset=True,targetSettings={})['portable'];assert portable['lipFinishVersion']==2 and 'lipLighting' not in portable
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Independent matte/gloss, selective highlight reduction, diffuse preservation, headroom, undo and saving passed')
