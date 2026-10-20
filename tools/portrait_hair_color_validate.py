from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-hair-color';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());recipe=copy.deepcopy(real);p=recipe['people'][0];s=p['settings'];s['hairColorVersion']=2;s['parameters']={};mask=Image.new('L',(320,240),0);ImageDraw.Draw(mask).rectangle((20,20,299,219),fill=255);s['automaticMasks']={'hair':encoded(mask)}
yy,xx=np.mgrid[:240,:320];gray=65+12*np.sin(xx*1.3)+160*np.exp(-((yy-85)/12)**2);gray=np.clip(gray,0,255).astype('uint8');source=OUT/'controlled.png';Image.fromarray(np.repeat(gray[:,:,None],3,axis=2)).save(source)
s['hairLighting']=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True)['hairLighting'];assert s['hairLighting'];s['colors']={'hair.color':[.22,.035,.018]};frames=[]
for strength in [0,100]:
 s['parameters']={'hair.color':strength};out=OUT/f'color-{strength}';call(op='portrait_geometry',recipe=recipe,source=str(source),size=[320,240],render=str(out.with_suffix('.png')),linearRender=str(out.with_suffix('.bin')));frames.append(np.fromfile(out.with_suffix('.bin'),np.float32).reshape(240,320,4))
a,b=frames;ya=a[:,:,:3]@np.array([.2627,.678,.0593]);yb=b[:,:,:3]@np.array([.2627,.678,.0593]);assert np.max(abs(ya-yb))<2e-6;assert np.array_equal(a[:20],b[:20]) and np.array_equal(a[:,:,3],b[:,:,3]);assert b[:,:,:3].max()<=1.00001
chroma=abs(b[:,:,0]/np.maximum(yb,.001)-a[:,:,0]/np.maximum(ya,.001));assert chroma[80:90,40:280].mean()<chroma[140:160,40:280].mean()*.6
s['hairShineVersion']=2;s['parameters']={'hair.shine':100};call(op='portrait_geometry',recipe=recipe,source=str(source),size=[320,240],render=str(OUT/'shine.png'),linearRender=str(OUT/'shine.bin'));shine=np.fromfile(OUT/'shine.bin',np.float32).reshape(240,320,4);gain=shine[:,:,0]-a[:,:,0];assert gain[80:90,40:280].mean()>gain[140:160,40:280].mean()*5 and gain.max()>.005;assert shine[:,:,:3].max()<=1.00001 and np.array_equal(shine[:20],a[:20]) and np.array_equal(shine[:,:,3],a[:,:,3])
s['parameters']={'hair.zone_test':100};s['hairZones']=[{'key':'hair.zone_test','name':'test','mode':'solid'}];s['colors']['hair.zone_test']=[.02,.1,.23];s['masks']={'hair.zone_test':encoded(Image.new('L',(320,240),255))};call(op='portrait_geometry',recipe=recipe,source=str(source),size=[320,240],render=str(OUT/'zone.png'),linearRender=str(OUT/'zone.bin'));zone=np.fromfile(OUT/'zone.bin',np.float32).reshape(240,320,4);assert np.array_equal(a[:20],zone[:20]) and not np.array_equal(a[30:200,30:290],zone[30:200,30:290])
recipe=real;s=recipe['people'][0]['settings'];s['parameters']={};source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=12);wait();call(op='portrait_state',styleSection=0);before=wait()['recipe'];call(op='portrait_state',click='portraitSwatch_73403e');after=wait()['recipe'];assert after['people'][0]['settings']['hairColorVersion']==2
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
call(op='portrait_state',styleSection=1);call(op='portrait_state',parameter='hair.shine',value=65);after=wait()['recipe'];assert after['people'][0]['settings']['hairShineVersion']==2
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'hair.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Hair chroma retains luminance and neutral highlights; zone clipping, swatch undo and saving passed')
