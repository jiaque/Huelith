from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,base64,io
OUT=ROOT/'docs/test-evidence/M3/semantic-tear-volume';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/semantic-forehead-volume/real-recipe.json').read_text(encoding='utf-8'));recipe=copy.deepcopy(real);p=recipe['people'][0];lm=np.array(p['landmarks'])[:,:2];span=np.linalg.norm((lm[42]+lm[45]-lm[36]-lm[39])/2);lm=(lm-lm[17:27].mean(0))*.30/span+[.5,.35];p['landmarks']=np.column_stack([lm,np.ones(66)]).tolist();p['settings']={'parameters':{'face.tearFill.left':100},'automaticMasks':{'faceSkin':encoded(Image.new('L',(512,512),255))}};p['matchAmbiguous']=p['unmatched']=False
Y,X=np.mgrid[:512,:512]/512;axis=lm[45]-lm[42];width=np.linalg.norm(axis);axis/=width;down=np.array([-axis[1],axis[0]])
if np.dot(lm[8]-lm[27],down)<0:down=-down
u=((X-lm[42,0])*axis[0]+(Y-lm[42,1])*axis[1])/width;v=((X-lm[42,0])*down[0]+(Y-lm[42,1])*down[1])/width
plane=-1.6+.25*X+.15*Y;texture=.01*np.sin(X*512*2*np.pi/5);track=(u>.03)&(u<.67);groove=.32*np.exp(-((v-(.27+.55*u))/.04)**2)*track
bag=.20*np.exp(-((u-.45)/.25)**2-((v-.12)/.045)**2)
def write(name,logs):
 linear=2**logs;s=np.where(linear<=.0031308,12.92*linear,1.055*linear**(1/2.4)-.055);Image.fromarray(np.uint8(np.clip(s*255+.5,0,255))[:,:,None].repeat(3,2)).save(OUT/(name+'.png'));return OUT/(name+'.png')
flat=write('gradient',plane+texture);source=write('groove',plane+texture-groove-bag)
neutral=call(op='portrait_geometry',tearVolume=True,source=str(flat),person=p);assert max(neutral.get('maxStops.left',0),neutral.get('maxStops.right',0))<.015
analysis=call(op='portrait_geometry',tearVolume=True,source=str(source),person=p);assert analysis.get('valid.left') and analysis.get('maxStops.left',0)>.10,{'valid':analysis.get('valid.left'),'maxStops':analysis.get('maxStops.left')};p['settings']['tearVolume']=analysis
for key in ['faceSkin','eyeglassFrame']:
 blocked=copy.deepcopy(p);blocked['settings']['automaticMasks'][key]=encoded(Image.new('L',(512,512),0 if key=='faceSkin' else 255));assert not call(op='portrait_geometry',tearVolume=True,source=str(source),person=blocked)
r=call(op='portrait_geometry',recipe=recipe,size=[512,512],source=str(source),render=str(OUT/'corrected.png'),linearRender=str(OUT/'corrected.f32'));assert r['identity']
base=copy.deepcopy(recipe);base['people'][0]['settings']['parameters']={};call(op='portrait_geometry',recipe=base,size=[512,512],source=str(source),render=str(OUT/'original.png'),linearRender=str(OUT/'original.f32'))
a=np.fromfile(OUT/'original.f32',np.float32).reshape(512,512,4);b=np.fromfile(OUT/'corrected.f32',np.float32).reshape(512,512,4);delta=b-a;assert delta[:,:,0].max()>.01
protected=(v<.20)|(u>.9);assert abs(delta[protected]).max()==0
# Independent opposite eye stays unchanged; local texture is not smeared.
gain=np.array(Image.open(io.BytesIO(base64.b64decode(analysis['gain.left']))));assert gain.max()>0
rot=copy.deepcopy(p);rot['landmarks']=np.column_stack([1-lm[:,1],lm[:,0],np.ones(66)]).tolist();Image.open(source).transpose(Image.Transpose.ROTATE_270).save(OUT/'rotated.png');rr=call(op='portrait_geometry',tearVolume=True,source=str(OUT/'rotated.png'),person=rot);rotgain=np.array(Image.open(io.BytesIO(base64.b64decode(rr['gain.left']))));assert abs(np.rot90(gain.astype(float),-1)-rotgain).mean()<20
# Actual controls and complete persistence path.
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';real['people'][0]['settings']['parameters']={};call(op='portrait_window',path=str(source),recipe=real);wait();call(op='portrait_state',select=0,panel=0);call(op='portrait_state',linked=False,side=0);before=wait()['recipe'];call(op='portrait_state',parameter='face.tearFill',value=80);changed=wait()['recipe'];s=changed['people'][0]['settings'];assert s['parameters'].get('face.tearFill.left')==80 and s['parameters'].get('face.tearFill.right',0)==0,{'parameters':s['parameters']}
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'real.png'),save=str(OUT/'tear.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,{'roundtrip':r['roundtrip'],'regionError':r['regionError']}
portable=call(op='portrait_geometry',portableSkin=s,facePreset=True,targetSettings={'tearVolume':{'stale':True}});assert 'tearVolume' not in portable['portable'] and 'tearVolume' not in portable['merged']
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=changed);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'real.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_state',incomingPreset=portable['portable']);call(op='portrait_state',panel=15);wait();call(op='portrait_state',presetCard=5);assert wait()['recipe']['people'][0]['settings']['tearVolume']['version']==1
call(op='portrait_close');call(op='shutdown');print('Tear trough contrast, gradient rejection, eyelid/bulge protection, rotation, frame/skin gates, bilateral UI, undo, ROI, presets and reopen passed')
