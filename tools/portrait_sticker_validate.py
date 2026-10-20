from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-sticker';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text(encoding='utf-8'));recipe=copy.deepcopy(real);p=recipe['people'][0];s=p['settings'];s['parameters']={'makeup.sticker':100,'makeup.stickerSize':65};s['masks']={};s['automaticMasks']={'skin':encoded(Image.new('L',(512,512),255))};lm=np.array(p['landmarks'])[:,:2];lm=(lm-lm.mean(axis=0))*.65/(lm.max(axis=0)-lm.min(axis=0)).max()+.5;p['landmarks']=lm.tolist()
def mask(name):
 path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=[512,512],styleMask='makeup.sticker',mask=str(path));return np.array(Image.open(path))
shapes=[]
for material in ['heart','star','flower']:
 s['stickerMaterial']=material;a=mask(material);assert a.max()>200 and np.count_nonzero(a)>50;shapes.append(a)
assert not np.array_equal(shapes[0],shapes[1]) and not np.array_equal(shapes[1],shapes[2])
s['stickerMaterial']='heart';p['landmarks']=np.column_stack([1-lm[:,1],lm[:,0]]).tolist();b=mask('rotated-face');assert np.abs(np.rot90(shapes[0],-1).astype(float)-b).mean()<.1;p['landmarks']=lm.tolist()
s['parameters']['makeup.stickerAngle']=25;assert not np.array_equal(shapes[0],mask('rotation'));s['parameters']['makeup.stickerAngle']=0
s['masks']={'makeup.sticker':encoded(Image.new('L',(512,512),0))};assert mask('erased').max()==0;s['masks']={};s['automaticMasks']['eyeglassFrame']=encoded(Image.new('L',(512,512),255));assert mask('occluded').max()==0
# Actual ready-to-use cards, source-coordinate placement, transform and erasure.
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=copy.deepcopy(real);recipe['people'][0]['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=9);before=wait()['recipe'];call(op='portrait_state',click='portraitSticker_heart');applied=wait()['recipe'];assert applied['people'][0]['settings']['stickerMaterial']=='heart' and applied['people'][0]['settings']['parameters']['makeup.sticker']==85
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();call(op='portrait_state',parameter='makeup.stickerAngle',value=20);wait();call(op='portrait_state',click='portraitStickerColor_e2b34b');before=wait()['recipe']
call(op='portrait_geometry',recipe=before,size=[512,512],styleMask='makeup.sticker',mask=str(OUT/'placement.png'));a=np.array(Image.open(OUT/'placement.png'));yy,xx=np.mgrid[:512,:512];center=np.array([(xx*a).sum()/a.sum(),(yy*a).sum()/a.sum()])/512
call(op='portrait_state',click='portraitPlaceSticker');call(op='portrait_state',paintPath=[center.tolist(),(center+[.012,.008]).tolist()]);moved=wait()['recipe'];assert moved['people'][0]['settings']['parameters']['makeup.stickerX']!=before['people'][0]['settings']['parameters'].get('makeup.stickerX',0)
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'sticker.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,{'roundtrip':r['roundtrip'],'regionError':r['regionError']}
call(op='portrait_state',click='portraitEraseSticker');call(op='portrait_state',brushSize=5,paintPath=[center.tolist(),(center+[.01,.01]).tolist()]);erased=wait()['recipe'];assert 'makeup.sticker' in erased['people'][0]['settings']['masks'];call(op='portrait_state',history=-1);assert wait()['recipe']==moved
portable=call(op='portrait_geometry',portableSkin=moved['people'][0]['settings'],makeupPreset=True,targetSettings={})['merged'];assert portable['stickerMaterial']=='heart' and portable['parameters']['makeup.stickerAngle']==20
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=moved);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'workflow.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Original vector shapes, face rotation, transform, occlusion, actual cards/placement/eraser, undo, ROI, presets and reopen passed')
