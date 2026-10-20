from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-mole';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text(encoding='utf-8'));recipe=copy.deepcopy(real);p=recipe['people'][0];s=p['settings'];s['parameters']={'makeup.mole':100,'makeup.moleSize':65};s['masks']={};s['automaticMasks']={'skin':encoded(Image.new('L',(512,512),255))}
lm=np.array(p['landmarks']);lm=(lm-lm.mean(axis=0))*.65/(lm.max(axis=0)-lm.min(axis=0)).max()+.5;p['landmarks']=lm.tolist()
def mask(name):
 path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=[512,512],styleMask='makeup.mole',mask=str(path));return np.array(Image.open(path))
a=mask('mask');assert a.max()>150 and 0<np.count_nonzero(a)<1000
p['landmarks']=np.column_stack([1-lm[:,1],lm[:,0]]).tolist();b=mask('rotated');assert np.abs(np.rot90(a,-1).astype(float)-b).mean()<.01;p['landmarks']=lm.tolist()
s['masks']={'makeup.mole':encoded(Image.new('L',(512,512),0))};assert mask('erased').max()==0;s['masks']={}
s['automaticMasks']['eyeglassFrame']=encoded(Image.new('L',(512,512),255));assert mask('occluded').max()==0;s['automaticMasks'].pop('eyeglassFrame')
s['parameters']['makeup.moleFeather']=100;b=mask('soft');assert not np.array_equal(a,b)
# Above the analysis-mask resolution, the pigment boundary is evaluated at output pixels.
large=OUT/'large-texture.png';row=(130+20*np.sin(np.arange(3072)*.6)).astype('uint8');gray=np.broadcast_to(row,(3072,3072));Image.fromarray(np.repeat(gray[:,:,None],3,axis=2)).save(large)
call(op='portrait_geometry',styleOnly=str(OUT/'large-mole.png'),source=str(large),recipe=recipe)
painted=np.array(Image.open(OUT/'large-mole.png').convert('RGB'));change=gray.astype(float)-painted[:,:,0];active=change>5;assert active.sum()>100 and active.sum()<10000;assert painted[:,:,0][active].std()>5
assert np.all(change>=-1) and np.all(painted[:,:,0][active]>gray[active]*.1)
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=copy.deepcopy(real);recipe['people'][0]['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=7);wait();call(op='portrait_state',click='portraitPlaceMole');before=wait()['recipe'];assert before['people'][0]['settings']['parameters']['makeup.mole']==70
lm=np.array(before['people'][0]['landmarks'])[:,:2];w,h=Image.open(source).size;pixel=lm*[w,h];axis=pixel[42:48].mean(axis=0)-pixel[36:42].mean(axis=0);down=np.array([-axis[1],axis[0]]);center=pixel[30]+axis*.45+down*.65;end=(center+axis*.12-down*.08)/[w,h]
call(op='portrait_state',paintPath=[(center/[w,h]).tolist(),end.tolist()]);moved=wait()['recipe'];params=moved['people'][0]['settings']['parameters'];assert abs(params['makeup.moleX']-12)<2 and abs(params['makeup.moleY']+8)<2,params
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();call(op='portrait_state',parameter='makeup.moleSize',value=35);wait();call(op='portrait_state',click='portraitMoleColor_694332');after=wait()['recipe'];assert len(after['people'][0]['settings']['colors']['makeup.mole'])==3
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'mole.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
portable=call(op='portrait_geometry',portableSkin=after['people'][0]['settings'],makeupPreset=True,targetSettings={})['portable'];assert portable['parameters']['makeup.mole']==70 and 'makeup.mole' in portable['colors']
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'workflow.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Mole local anatomy, rotation, feather, occlusion, actual placement/color, undo, ROI, portable settings and reopen passed')
