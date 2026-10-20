from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-material-sides';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+45
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';base=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());person=base['people'][0];analysis=call(op='portrait_geometry',source=str(source),person=person,repairAnalysis=True)
for key in ['irisMaterials','browMaterials']:person['settings'][key]=analysis[key]
import copy
cases=[('browMaterial','straight','makeup.browShape',1),('linerMaterial','wing','makeup.liner',2),('lashMaterial','long','makeup.lash',2),('creaseMaterial','parallel','makeup.crease',2),('catchMaterial','window','makeup.catchlight',4)]
for material,choice,parameter,section in cases:
 recipe=copy.deepcopy(base);s=recipe['people'][0]['settings'];s['parameters']={parameter+'.right':65,parameter+'.left':0};s['side']=0;s['linked']=False
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=section);before=wait()['recipe'];call(op='portrait_state',source=str(source),render=str(OUT/(material+'-before.png')))
 call(op='portrait_state',click='portraitMaterialCard_'+material+'_'+choice);after=wait()['recipe'];a=after['people'][0]['settings'];assert a[material+'.left']==choice and a['parameters'][parameter+'.left']==70
 assert a['parameters'][parameter+'.right']==65
 call(op='portrait_state',source=str(source),render=str(OUT/(material+'-after.png')),checkRegion=True,save=str(OUT/(material+'.huelith')))
 im1=np.asarray(Image.open(OUT/(material+'-before.png')));im2=np.asarray(Image.open(OUT/(material+'-after.png')));lm=recipe['people'][0]['landmarks'];split=int((lm[39][0]+lm[42][0])*.5*im1.shape[1]);assert np.array_equal(im1[:,:split],im2[:,:split]),material+' changed opposite side pixels';assert not np.array_equal(im1[:,split:],im2[:,split:]),material+' did not apply'
 call(op='portrait_state',history=-1);assert wait()['recipe']==before
 call(op='portrait_state',history=1);assert wait()['recipe']==after
 portable=call(op='portrait_geometry',portableSkin=a,makeupPreset=True,targetSettings={})
 assert portable['portable'][material+'.left']==choice and portable['merged'][material+'.left']==choice
 if material=='browMaterial':
  call(op='portrait_state',parameter='makeup.brow',value=40);wait();call(op='portrait_state',source=str(source),render=str(OUT/'color-before.png'))
  call(op='portrait_state',click='portraitSwatch_755647');colored=wait()['recipe'];assert 'makeup.brow.left' in colored['people'][0]['settings']['colors']
  call(op='portrait_state',source=str(source),render=str(OUT/'color-after.png'));c1=np.asarray(Image.open(OUT/'color-before.png'));c2=np.asarray(Image.open(OUT/'color-after.png'));assert np.array_equal(c1[:,:split],c2[:,:split]) and not np.array_equal(c1[:,split:],c2[:,split:])
  portable=call(op='portrait_geometry',portableSkin=colored['people'][0]['settings'],makeupPreset=True,targetSettings={});assert portable['merged']['colors']['makeup.brow.left']==colored['people'][0]['settings']['colors']['makeup.brow.left']
  # A linked color choice deliberately replaces both individual overrides.
  call(op='portrait_state',linked=True);wait();call(op='portrait_state',click='portraitSwatch_49372b');linked=wait()['recipe']['people'][0]['settings']['colors'];assert 'makeup.brow' in linked and 'makeup.brow.left' not in linked and 'makeup.brow.right' not in linked
 call(op='portrait_close')
# Applying a legacy shared preset replaces previous side overrides, rather than silently ignoring it.
target={'colors':{'makeup.brow.left':[.1,.2,.3]},'browMaterial.left':'straight'}
preset={'parameters':{'makeup.brow.left':30,'makeup.brow.right':30},'colors':{'makeup.brow':[.2,.1,.05]},'browMaterial':'arched'}
r=call(op='portrait_geometry',portableSkin=preset,makeupPreset=True,targetSettings=target)['merged'];assert r['browMaterial']=='arched' and 'browMaterial.left' not in r and 'makeup.brow.left' not in r['colors']
# Coverage brushes cannot replace eye geometry with an unrestricted pigment rectangle.
import base64,io
buf=io.BytesIO();Image.new('L',(1200,750),255).save(buf,format='PNG');white=base64.b64encode(buf.getvalue()).decode()
for key in ['makeup.shadow','makeup.liner','makeup.lash','makeup.crease','makeup.aegyosal']:
 recipe=copy.deepcopy(base);s=recipe['people'][0]['settings'];s['parameters']={key+'.left':70};s['eyeShadowVersion']=s['eyeLinerVersion']=s['eyeLashVersion']=2;s['masks']={}
 call(op='portrait_geometry',recipe=recipe,source=str(source),size=[1200,750],render=str(OUT/'coverage-before.png'))
 s['masks'][key+'.left']=white
 call(op='portrait_geometry',recipe=recipe,source=str(source),size=[1200,750],render=str(OUT/'coverage-after.png'))
 assert np.array_equal(np.asarray(Image.open(OUT/'coverage-before.png')),np.asarray(Image.open(OUT/'coverage-after.png'))),key
call(op='shutdown');print('Single-side materials and colors preserve opposite-side pixels; undo, zero-strength activation and portable presets passed')
