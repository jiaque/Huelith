from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-accessory-material';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());recipe=copy.deepcopy(real);p=recipe['people'][0];p['face']=[.1,.1,.8,.8];s=p['settings'];s['hairAccessoryVersion']=2;s['parameters']={'hair.accessory':100,'hair.accessorySize':85,'hair.accessoryX':-35,'hair.accessoryY':65};s['colors']={'hair.accessory':[.3,.15,.04]};s['masks']={};source=OUT/'source.png';Image.new('RGB',(512,512),(45,45,45)).save(source)
for material in ['clip','flower','bow']:
 s['hairMaterial']=material;frames=[]
 for roughness in [0,100]:
  s['parameters']['hair.accessoryRoughness']=roughness;name=material+'-'+str(roughness);call(op='portrait_geometry',recipe=recipe,source=str(source),size=[512,512],render=str(OUT/(name+'.png')),linearRender=str(OUT/(name+'.bin')));frames.append(np.fromfile(OUT/(name+'.bin'),np.float32).reshape(512,512,4))
 a,b=frames;assert not np.array_equal(a,b);assert np.array_equal(a[:,:,3],b[:,:,3]);assert a[:,:,:3].max()<=1 and b[:,:,:3].max()<=1
 assert a[180:330,120:390,:3].std()>.03
 if material=='clip':assert a[:,:,0].max()>b[:,:,0].max()+.05
# Known occluders isolate ordering from segmentation quality.
skin=Image.new('L',(512,512),0);ImageDraw.Draw(skin).rectangle((250,0,265,511),fill=255);hair=Image.new('L',(512,512),0);ImageDraw.Draw(hair).rectangle((0,0,249,511),fill=255);frame=Image.new('L',(512,512),0);ImageDraw.Draw(frame).rectangle((0,250,511,254),fill=255);s['automaticMasks']={'makeupSkin':encoded(skin),'hair':encoded(hair),'eyeglassFrame':encoded(frame)}
def occlusion(mode):
 s['hairAccessoryOcclusion']=mode;path=OUT/(mode+'-mask.png');call(op='portrait_geometry',recipe=recipe,size=[512,512],styleMask='hair.accessory',mask=str(path));return np.asarray(Image.open(path))
a=occlusion('manual');b=occlusion('skin');c=occlusion('behindHair');assert b[:,250:266].max()==0 and b[250:255].max()==0;assert 0<c.sum()<b.sum()<a.sum();assert c[:,:250].max()==0 and c[:,280:].max()>200
preset={'parameters':{'hair.smooth':30,'hair.accessory':70},'hairSmoothVersion':2,'hairEdgeVersion':1,'hairAccessoryVersion':2};merged=call(op='portrait_geometry',portableSkin=preset,hairPreset=True,targetSettings={})['merged'];assert merged['hairSmoothVersion']==2 and merged['hairEdgeVersion']==1 and merged['hairAccessoryVersion']==2
merged=call(op='portrait_geometry',portableSkin={'parameters':{'makeup.lip':30}},makeupPreset=True,targetSettings={'hairEdgeVersion':1})['merged'];assert merged['hairEdgeVersion']==1
recipe=real;s=recipe['people'][0]['settings'];s['parameters']={};source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=12);wait();call(op='portrait_state',styleSection=3);before=wait()['recipe'];call(op='portrait_state',click='portraitMaterialCard_hairMaterial_bow');after=wait()['recipe'];assert after['people'][0]['settings']['hairAccessoryVersion']==2
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);wait();call(op='portrait_state',accessoryOcclusion='behindHair');assert wait()['recipe']['people'][0]['settings']['hairAccessoryOcclusion']=='behindHair';call(op='portrait_state',history=-1);assert wait()['recipe']==after
call(op='portrait_state',parameter='hair.accessoryRoughness',value=65);after=wait()['recipe'];call(op='portrait_state',capture=str(OUT/'cards-ui.png'))
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'material.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Distinct original materials, roughness, alpha, actual cards, preset versions, undo and saving passed')
