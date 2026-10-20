"""Source-driven cheek tonal correction and independent geometry composition."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,base64,io
OUT=ROOT/'docs/test-evidence/M3/semantic-orbital-lift';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'tone-settings'))
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/semantic-forehead-volume/real-recipe.json').read_text(encoding='utf-8'));recipe=copy.deepcopy(real);p=recipe['people'][0];lm=np.array(p['landmarks'])[:,:2];span=np.linalg.norm(lm[16]-lm[0]);lm=(lm-lm[30])*.7/span+[.5,.48];p['landmarks']=np.column_stack([lm,np.ones(66)]).tolist();p['settings']={'parameters':{'face.orbitalTone.left':100},'automaticMasks':{'faceSkin':encoded(Image.new('L',(512,512),255))}};p['matchAmbiguous']=p['unmatched']=False
axis=(lm[42]+lm[45]-lm[36]-lm[39])*.5;axis/=np.linalg.norm(axis);center=lm[45]+axis*.10*np.linalg.norm(lm[16]-lm[0])
Y,X=np.mgrid[:512,:512]/512;plane=-1.6+.25*X+.15*Y;texture=.008*np.sin(X*512*2*np.pi/5);basin=.4*np.exp(-((X-center[0])/.020)**2-((Y-center[1])/.028)**2)
def write(name,logs):
 linear=2**logs;s=np.where(linear<=.0031308,12.92*linear,1.055*linear**(1/2.4)-.055);Image.fromarray(np.uint8(np.clip(s*255+.5,0,255))[:,:,None].repeat(3,2)).save(OUT/(name+'.png'));return OUT/(name+'.png')
flat=write('gradient',plane+texture);source=write('basin',plane+texture-basin)
neutral=call(op='portrait_geometry',orbitalVolume=True,source=str(flat),person=p);assert neutral.get('valid.left') and neutral.get('maxStops.left',1)<.015
analysis=call(op='portrait_geometry',orbitalVolume=True,source=str(source),person=p);assert analysis.get('valid.left') and analysis.get('maxStops.left',0)>.10,{'maxStops':analysis.get('maxStops.left')};p['settings']['orbitalVolume']=analysis
for key in ['faceSkin','eyeglassFrame']:
 blocked=copy.deepcopy(p);blocked['settings']['automaticMasks'][key]=encoded(Image.new('L',(512,512),0 if key=='faceSkin' else 255));assert not call(op='portrait_geometry',orbitalVolume=True,source=str(source),person=blocked)
r=call(op='portrait_geometry',recipe=recipe,size=[512,512],source=str(source),render=str(OUT/'corrected.png'),linearRender=str(OUT/'corrected.f32'));assert r['identity']
base=copy.deepcopy(recipe);base['people'][0]['settings']['parameters']={};call(op='portrait_geometry',recipe=base,size=[512,512],source=str(source),render=str(OUT/'original.png'),linearRender=str(OUT/'original.f32'))
a=np.fromfile(OUT/'original.f32',np.float32).reshape(512,512,4);b=np.fromfile(OUT/'corrected.f32',np.float32).reshape(512,512,4);delta=b-a;assert delta[:,:,0].max()>.01
for index in list(range(36,48))+[31,35,58,62]:
 x,y=np.rint(lm[index]*512).astype(int);assert abs(delta[max(0,y-2):y+3,max(0,x-2):x+3]).max()==0,index
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';real['people'][0]['settings']['parameters']={}
call(op='portrait_window',path=str(source),recipe=real);wait();call(op='portrait_state',select=0,panel=0,linked=False,side=0);before=wait()['recipe'];call(op='portrait_state',parameter='face.orbitalTone',value=80);changed=wait()['recipe'];s=changed['people'][0]['settings'];assert s['parameters'].get('face.orbitalTone.left')==80 and s['parameters'].get('face.orbitalTone.right',0)==0
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();call(op='portrait_state',parameter='face.orbitalLift',value=80);changed=wait()['recipe']
r=call(op='portrait_state',source=str(source),render=str(OUT/'real-combined.png'),save=str(OUT/'orbital-combined.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,{'roundtrip':r['roundtrip'],'regionError':r['regionError']}
portable=call(op='portrait_geometry',portableSkin=s,facePreset=True,targetSettings={'orbitalVolume':{'stale':True}});assert 'orbitalVolume' not in portable['portable'] and 'orbitalVolume' not in portable['merged']
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=changed);wait();call(op='portrait_state',source=str(source),render=str(OUT/'combined-reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'real-combined.png')),np.array(Image.open(OUT/'combined-reopened.png')))
call(op='portrait_state',incomingPreset=portable['portable']);call(op='portrait_state',panel=15);wait();call(op='portrait_state',presetCard=5);assert wait()['recipe']['people'][0]['settings']['orbitalVolume']['version']==1
call(op='portrait_close');call(op='shutdown');print('Orbital tonal basin, gradient rejection, fixed features, skin/frame gates, independent controls, ROI, save, reopen and preset recomputation passed')
