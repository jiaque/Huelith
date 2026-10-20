from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,base64,io
OUT=ROOT/'docs/test-evidence/M3/semantic-foundation';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());recipe=copy.deepcopy(real);p=recipe['people'][0];p['face']=[.1,.1,.8,.8];s=p['settings'];s['parameters']={}
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
mask=Image.new('L',(320,240),0);d=ImageDraw.Draw(mask);d.rectangle((32,24,287,215),fill=255);d.rectangle((105,70,150,95),fill=0);d.rectangle((120,160,200,185),fill=0);s['automaticMasks']={'skin':encoded(mask),'faceSkin':encoded(mask)}
yy,xx=np.mgrid[:240,:320];rgb=np.zeros((240,320,3),np.float32);shade=.75+.2*xx/320;texture=2*np.sin(xx*2)*np.sin(yy*2);patch=np.exp(-((xx-220)/35)**2-((yy-125)/28)**2)
rgb[:,:,0]=165*shade+texture+patch*24;rgb[:,:,1]=120*shade+texture-patch*8;rgb[:,:,2]=95*shade+texture
source=OUT/'controlled.png';Image.fromarray(np.clip(rgb,0,255).astype('uint8')).save(source);frames=[]
for strength in [0,100]:
 s['parameters']={'makeup.foundation':strength};out=OUT/f'controlled-{strength}';call(op='portrait_geometry',recipe=recipe,source=str(source),size=[320,240],render=str(out.with_suffix('.png')),linearRender=str(out.with_suffix('.bin')));frames.append(np.fromfile(out.with_suffix('.bin'),np.float32).reshape(240,320,4))
a,b=frames;weights=np.array([.2627,.678,.0593]);ya=a[:,:,:3]@weights;yb=b[:,:,:3]@weights
assert np.max(abs(ya-yb))<2e-6 and np.array_equal(a[:,:,3],b[:,:,3])
assert np.array_equal(a[75:90,110:145],b[75:90,110:145]) and np.array_equal(a[:20],b[:20])
red_a=a[115:135,210:230,0]/ya[115:135,210:230];red_b=b[115:135,210:230,0]/yb[115:135,210:230];assert red_b.mean()<red_a.mean()-.005
assert np.array_equal(a[165:180,125:195],b[165:180,125:195])
s['masks']={'makeup.foundation':encoded(Image.new('L',(320,240),0))};call(op='portrait_geometry',recipe=recipe,source=str(source),size=[320,240],render=str(OUT/'erased.png'));assert np.array_equal(np.asarray(Image.open(OUT/'erased.png')),np.asarray(Image.open(OUT/'controlled-0.png')))
recipe=real;recipe['people'][0]['settings']['parameters']={};source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=6);before=wait()['recipe'];call(op='portrait_state',click='portraitFoundationNatural');after=wait()['recipe'];assert after['people'][0]['settings']['parameters']['makeup.foundation']==35
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'foundation.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
portable=call(op='portrait_geometry',portableSkin=after['people'][0]['settings'],makeupPreset=True,targetSettings={})['portable'];assert portable['parameters']['makeup.foundation']==35
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Foundation reduces low-frequency chroma unevenness, preserves luminance/alpha/exclusions, supports erase, undo and saving')
