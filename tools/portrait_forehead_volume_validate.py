from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-forehead-volume';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text(encoding='utf-8'));recipe=copy.deepcopy(real);p=recipe['people'][0];lm=np.array(p['landmarks'])[:,:2];span=np.linalg.norm((lm[42]+lm[45]-lm[36]-lm[39])/2);lm=(lm-lm[17:27].mean(0))*.25/span+[.5,.55];p['landmarks']=np.column_stack([lm,np.ones(66)]).tolist();p['settings']={'parameters':{'face.foreheadFill':100},'automaticMasks':{'faceSkin':encoded(Image.new('L',(512,512),255))}};p['matchAmbiguous']=p['unmatched']=False
Y,X=np.mgrid[:512,:512]/512
texture=.012*np.sin(X*512*2*np.pi/5)
plane=-1.6+.25*(X-.5)+.15*(Y-.5)
basin=.32*np.exp(-((X-.5)/.055)**2-((Y-.41)/.05)**2)
def write(name,logs):
 linear=2**logs;s=np.where(linear<=.0031308,12.92*linear,1.055*linear**(1/2.4)-.055);Image.fromarray(np.uint8(np.clip(s*255+.5,0,255))[:,:,None].repeat(3,2)).save(OUT/(name+'.png'));return OUT/(name+'.png')
flat=write('gradient',plane+texture);source=write('basin',plane+texture-basin)
neutral=call(op='portrait_geometry',foreheadVolume=True,source=str(flat),person=p);assert neutral['maxStops']<.015,neutral['maxStops']
analysis=call(op='portrait_geometry',foreheadVolume=True,source=str(source),person=p);assert analysis['maxStops']>.12,analysis.get('maxStops');p['settings']['foreheadVolume']=analysis
zero=copy.deepcopy(p);zero['settings']['automaticMasks']['faceSkin']=encoded(Image.new('L',(512,512),0));assert not call(op='portrait_geometry',foreheadVolume=True,source=str(source),person=zero)
blocked=copy.deepcopy(p);blocked['settings']['automaticMasks']['eyeglassFrame']=encoded(Image.new('L',(512,512),255));assert not call(op='portrait_geometry',foreheadVolume=True,source=str(source),person=blocked)
r=call(op='portrait_geometry',recipe=recipe,size=[512,512],source=str(source),render=str(OUT/'corrected.png'),linearRender=str(OUT/'corrected.f32'));assert r['identity']
base=copy.deepcopy(recipe);base['people'][0]['settings']['parameters']['face.foreheadFill']=0;call(op='portrait_geometry',recipe=base,size=[512,512],source=str(source),render=str(OUT/'original.png'),linearRender=str(OUT/'original.f32'))
a=np.fromfile(OUT/'original.f32',np.float32).reshape(512,512,4);b=np.fromfile(OUT/'corrected.f32',np.float32).reshape(512,512,4);delta=b-a
assert np.max(abs(delta[300:]))==0 # Eyebrows/eyes/mouth remain fixed, no global exposure.
assert delta[210,256,0]>.015
# High-frequency texture remains multiplicative, rather than being blurred away.
orig=np.log2(a[205:220,235:275,0]);out=np.log2(b[205:220,235:275,0]);high=lambda z:z[:,2:]-2*z[:,1:-1]+z[:,:-2];assert np.corrcoef(high(orig).ravel(),high(out).ravel())[0,1]>.98
# Actual source-derived control, undo, ROI and project persistence.
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png'
legacy=copy.deepcopy(real);legacy['people'][0]['settings']['parameters']={};call(op='portrait_window',path=str(source),recipe=legacy);wait();call(op='portrait_state',select=0,panel=0);prior=wait()['recipe'];call(op='portrait_state',parameter='face.foreheadFill',value=80);assert wait()['recipe']==prior;call(op='portrait_close')
os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe');os.environ['TMP']=os.environ['TEMP']=str(ROOT/'build/portrait-ai-tmp')
call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'));real=wait()['recipe'];assert len(real['people'])>0;call(op='portrait_close');(OUT/'real-recipe.json').write_text(json.dumps(real),encoding='utf-8');real['people'][0]['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=real);wait();call(op='portrait_state',select=0,panel=0);before=wait()['recipe'];call(op='portrait_state',parameter='face.foreheadFill',value=80);changed=wait()['recipe'];s=changed['people'][0]['settings'];assert s['parameters']['face.foreheadFill']==80 and s['foreheadVolume']['version']==1
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'real.png'),save=str(OUT/'forehead.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,{'roundtrip':r['roundtrip'],'regionError':r['regionError']}
portable=call(op='portrait_geometry',portableSkin=s,facePreset=True,targetSettings={'foreheadVolume':{'invalid':True}});assert 'foreheadVolume' not in portable['portable'] and 'foreheadVolume' not in portable['merged']
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=changed);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'real.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_state',incomingPreset=portable['portable']);call(op='portrait_state',panel=15);wait();call(op='portrait_state',presetCard=5);applied=wait()['recipe'];assert applied['people'][0]['settings']['foreheadVolume']['version']==1
call(op='portrait_close');call(op='shutdown');print('Forehead depression attenuation, gradient/texture preservation, frame and unknown-skin gates, no geometry change, UI undo, ROI and reopen passed')
