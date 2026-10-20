"""Bounded real-photo review set. Detection failures remain visible in the report."""
from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
from PIL import ImageOps
O=R/'docs/test-evidence/M3/quality';O.mkdir(parents=True,exist_ok=True)
paths=sorted((R/'research/m3/portraits').glob('*.jpg'))+[R/'research/m0/portrait-highres.jpg',R/'research/m2/instances/FudanPed00001.png']
report=[]
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp)
 for path in paths:
  item={'file':str(path.relative_to(R))};start=time.monotonic();stem=path.stem
  try:
   im=ImageOps.exif_transpose(Image.open(path)).convert('RGB');im.thumbnail((1200,1200));source=O/(stem+'-source.png');im.save(source)
   checked(op='portrait_window',path=str(source),models=str(R/'build/windows-release/models'))
   deadline=time.monotonic()+75
   while time.monotonic()<deadline:
    checked(op='pump',waitMs=50);r=checked(op='portrait_state')
    if r['ready']:break
   item.update(people=r['count'],status=r['status'])
   if not r['count']:report.append(item);print(stem,'NO FACE',flush=True);continue
   selected=max(range(r['count']),key=lambda i:r['recipe']['people'][i]['face'][2]*r['recipe']['people'][i]['face'][3])
   checked(op='portrait_state',select=selected,panel=7,click='portraitAnalyzeRepairs')
   r=checked(op='portrait_state');person=r['recipe']['people'][selected];item.update(teethStatus=person['settings'].get('teethStatus'),suggestions=len(person['settings'].get('blemishes',[])),bodyAmbiguous=person['bodyAmbiguous'],partialSkin=person['settings'].get('partialSkin',False))
   (O/(stem+'-recipe.json')).write_text(json.dumps(r['recipe'],indent=2))
   # Preserve a face crop with automatic teeth/skin boundaries and suggestions for visual review.
   import base64,io
   masks=person['settings']['automaticMasks'];overlay=im.convert('RGBA');a=np.array(overlay);skin=Image.open(io.BytesIO(base64.b64decode(masks['skin']))).resize(im.size);teeth=Image.open(io.BytesIO(base64.b64decode(masks['teeth']))).resize(im.size)
   w=np.array(skin)/255*.25;a[:,:,:3]=(a[:,:,:3]*(1-w[:,:,None])+np.array([40,140,255])*w[:,:,None]).astype('uint8');t=np.array(teeth)>100;a[t,:3]=[50,255,80];overlay=Image.fromarray(a);draw=ImageDraw.Draw(overlay)
   for s in person['settings']['blemishes']:
    x=s['x']*im.width;y=s['y']*im.height;draw.ellipse((x-3,y-3,x+3,y+3),outline='yellow',width=1)
   box=person['face'];crop=(max(0,int((box[0]-.1*box[2])*im.width)),max(0,int((box[1]-.2*box[3])*im.height)),min(im.width,int((box[0]+box[2]*1.1)*im.width)),min(im.height,int((box[1]+box[3]*1.1)*im.height)))
   overlay.crop(crop).convert('RGB').save(O/(stem+'-mask.jpg'))
   checked(op='portrait_state',panel=5,parameter='skin.smooth',value=35);checked(op='portrait_state',parameter='skin.texture',value=10)
   if not item['partialSkin']:
    checked(op='portrait_state',panel=10,parameter='teeth.yellow',value=35);checked(op='portrait_state',parameter='teeth.light',value=20)
   r=checked(op='portrait_state',source=str(source),render=str(O/(stem+'-after.png')),checkRegion=True);item['regionError']=r['regionError'];assert r['regionError']<2e-6, r['regionError']
   Image.open(O/(stem+'-after.png')).convert('RGB').crop(crop).save(O/(stem+'-after-crop.jpg'));im.crop(crop).save(O/(stem+'-before-crop.jpg'))
   item['seconds']=round(time.monotonic()-start,2);print(stem,item['people'],item['suggestions'],item['seconds'],flush=True)
  except Exception as e:item['error']=str(e);print(stem,'ERROR',str(e),flush=True)
  finally:checked(op='portrait_close')
  report.append(item);(O/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 checked(op='shutdown')
(O/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
for page in range((len(paths)+7)//8):
 canvas=Image.new('RGB',(1200,4*330),'#25292e');draw=ImageDraw.Draw(canvas)
 for i,path in enumerate(paths[page*8:page*8+8]):
  x=(i%2)*600;y=(i//2)*330;draw.text((x+3,y+3),path.stem,fill='white')
  for j,suffix in enumerate(['before-crop','after-crop','mask']):
   p=O/(path.stem+'-'+suffix+'.jpg')
   if p.exists():im=Image.open(p);im.thumbnail((196,290));canvas.paste(im,(x+j*200,y+24))
 canvas.save(O/f'review-{page+1}.jpg')
print('Review set complete:',len(report),'photos; inspect failures and crops before acceptance',flush=True)
