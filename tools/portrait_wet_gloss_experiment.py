from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/wet-gloss-experiment';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
import cv2
results=[]
for name in ['leroy-chiao','jessica-watkins','christina-koch']:
 recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0];s=p['settings'];s['parameters']={};source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=Image.open(source).size
 lighting=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True)['hairLighting'];s['hairLighting']=lighting;s['hairShineVersion']=2
 call(op='portrait_geometry',recipe=recipe,source=str(source),size=list(size),render=str(OUT/(name+'-base.png')),linearRender=str(OUT/(name+'-base.bin')));original=np.fromfile(OUT/(name+'-base.bin'),np.float32).reshape(size[1],size[0],4)
 s['parameters']={'hair.shine':100};call(op='portrait_geometry',recipe=recipe,source=str(source),size=list(size),render=str(OUT/(name+'-shine.png')),linearRender=str(OUT/(name+'-shine.bin')))
 hair=np.asarray(Image.open(io.BytesIO(base64.b64decode(s['automaticMasks']['hair']))).convert('L').resize(size,Image.Resampling.BILINEAR))/255.;highlight=np.asarray(Image.open(io.BytesIO(base64.b64decode(lighting['highlight']))).convert('L').resize(size,Image.Resampling.BILINEAR))/255.
 radius=max(2,p['face'][2]*size[0]*.045);weight=cv2.GaussianBlur(hair,(0,0),radius);broad=cv2.GaussianBlur(highlight*hair,(0,0),radius)/np.maximum(weight,.01);broad=np.minimum(1,broad*1.5)*hair
 light=original[:,:,:3]@np.array([.2627,.678,.0593]);peak=original[:,:,:3].max(2);gain=broad*np.minimum(light*.35,.08)*np.maximum(0,1-peak);candidate=original.copy();candidate[:,:,:3]+=gain[:,:,None]
 # Convert working Rec.2020 linear RGB to display sRGB for a reproducible contact sheet.
 matrix=np.array([[1.660491,-.587641,-.072850],[-.124550,1.132900,-.008349],[-.018151,-.100579,1.118730]])
 rgb=np.clip(candidate[:,:,:3]@matrix.T,0,1);rgb=np.where(rgb<=.0031308,rgb*12.92,1.055*np.power(rgb,1/2.4)-.055);Image.fromarray(np.round(rgb*255).astype('uint8')).save(OUT/(name+'-candidate.png'))
 shine=np.fromfile(OUT/(name+'-shine.bin'),np.float32).reshape(original.shape);dg=(shine[:,:,:3]-original[:,:,:3]).mean(2);region=hair>.8;fraction=float(np.mean(abs(gain[region]-dg[region])>.003));results.append({'sample':name,'different_pixel_fraction':fraction,'candidate_peak_gain':float(gain.max())})
 sheet=Image.new('RGB',(900,360),(30,34,38));draw=ImageDraw.Draw(sheet);f=p['face'];box=(int((f[0]-.15*f[2])*size[0]),max(0,int((f[1]-.6*f[3])*size[1])),int((f[0]+1.15*f[2])*size[0]),int((f[1]+.2*f[3])*size[1]))
 for i,suffix in enumerate(['base','shine','candidate']):
  im=Image.open(OUT/f'{name}-{suffix}.png').crop(box);im.thumbnail((300,330));sheet.paste(im,(i*300,25));draw.text((i*300+8,7),suffix,fill='white')
 sheet.save(OUT/(name+'-comparison.png'))
(OUT/'metrics.json').write_text(json.dumps(results,indent=2));call(op='shutdown');print(json.dumps(results))
