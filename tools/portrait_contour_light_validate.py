from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-contour-light';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text(encoding='utf-8'))
recipe=copy.deepcopy(real);p=recipe['people'][0];s=p['settings'];s['parameters']={};s['masks']={};s['contourLightVersion']=2
# Expand the measured landmarks for accurate spatial checks, with known skin coverage.
lm=np.array(p['landmarks']);lm=(lm-lm.mean(axis=0))*.65/(lm.max(axis=0)-lm.min(axis=0)).max()+.5;p['landmarks']=lm.tolist()
s['automaticMasks']={'skin':encoded(Image.new('L',(512,512),255))}
def mask(name,key):
 path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=[512,512],styleMask=key,mask=str(path));return np.array(Image.open(path))
metrics={}
for key in ['contour.highlight','contour.shadow']:
 a=mask(key,key);assert a.max()>100
 # 90 degree rotation of all geometry must rotate the effect, not keep screen ellipses.
 p['landmarks']=np.column_stack([1-lm[:,1],lm[:,0]]).tolist();b=mask(key+'-rotated',key);p['landmarks']=lm.tolist()
 error=np.abs(np.rot90(a,-1).astype(float)-b).mean();assert error<.15,error;metrics[key+'RotationMean']=error
 for start,stop in [(36,42),(42,48),(48,60)]:
  center=lm[start:stop].mean(axis=0)*512;assert a[int(center[1]),int(center[0])]==0
 s['masks']={key:encoded(Image.new('L',(512,512),255))};assert np.array_equal(a,mask('full-'+key,key))
 s['masks']={key:encoded(Image.new('L',(512,512),0))};assert mask('erase-'+key,key).max()==0;s['masks']={}
 s['automaticMasks']={};assert mask('unknown-'+key,key).max()==0;s['automaticMasks']={'skin':encoded(Image.new('L',(512,512),255))}
# Textured linear signal: exposure preserves chromatic ratios and alpha; no paint fill.
source=OUT/'source.png';yy,xx=np.mgrid[:512,:512];gray=(125+15*np.sin(xx*.7)*np.cos(yy*.7)).astype('uint8');Image.fromarray(np.stack([gray,gray//2,gray//3],axis=2)).save(source)
def render(name,parameters):
 s['parameters']=parameters;call(op='portrait_geometry',recipe=recipe,source=str(source),size=[512,512],render=str(OUT/(name+'.png')),linearRender=str(OUT/(name+'.bin')));return np.fromfile(OUT/(name+'.bin'),np.float32).reshape(512,512,4)
a=render('base',{})
for key in ['contour.highlight','contour.shadow']:
 b=render('render-'+key,{key:100});assert np.array_equal(a[:,:,3],b[:,:,3]);assert np.isfinite(b).all()
 gains=b[:,:,:3]/np.maximum(a[:,:,:3],1e-8);assert np.max(gains.max(axis=2)-gains.min(axis=2))<1e-5
 if key.endswith('highlight'):assert gains.min()>=1-1e-6 and gains.max()>1.05 and b[:,:,:3].max()<=1
 else:assert gains.max()<=1+1e-6 and .70<gains.min()<.98
# Portable presets retain the algorithm, but no source masks or illumination.
preset={'parameters':{'contour.highlight':40,'contour.shadow':20},'contourLightVersion':2}
merged=call(op='portrait_geometry',portableSkin=preset,contourPreset=True,targetSettings={})['merged'];assert merged['contourLightVersion']==2 and merged['parameters']==preset['parameters']
# Actual controls, undo transaction, ROI and saved project roundtrip.
recipe=real;s=recipe['people'][0]['settings'];s['parameters']={};source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png'
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=13);before=wait()['recipe']
call(op='portrait_state',parameter='contour.highlight',value=85);after=wait()['recipe'];assert after['people'][0]['settings']['contourLightVersion']==2
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);wait();call(op='portrait_state',parameter='contour.shadow',value=70);after=wait()['recipe']
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'contour.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'workflow.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');(OUT/'metrics.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8');print('Contour fields: rotated anatomy, eye/mouth exclusion, erasure, texture/chroma, actual controls, undo, ROI and save passed')
