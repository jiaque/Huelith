from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-belly';OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg';recipe=json.loads((source.parent/'mediapipe-pose-recipe.json').read_text(encoding='utf-8'))
call(op='init',settings=str(OUT/'settings'));p=recipe['people'][0];p['settings']['waistGeometryVersion']=2
import copy,base64,io
pose=np.array(p['settings']['pose']);size=np.array(Image.open(source).size);a=pose[[11,12],:2].mean(0)*(size-1);b=pose[[23,24],:2].mean(0)*(size-1);axis=b-a;length=np.linalg.norm(axis);axis/=length;normal=np.array([-axis[1],axis[0]])
mask=np.array(Image.open(io.BytesIO(base64.b64decode(p['bodyMask']))));center=a+(b-a)*.725
for radius in np.arange(1,length):
 q=(center+normal*radius)/(size-1)*[mask.shape[1],mask.shape[0]];x,y=q.astype(int)
 if mask[y,x]<160:break
front=(center+normal*(radius-1))/(size-1);p['settings']['abdomenFront']=front.tolist()
fields={}
for key in ['body.abdomen','body.belly']:
 p['settings']['parameters']={key:100}
 r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/(key+'.png')));w,h=r['meshSize'];f=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2];fields[key]=f
 points=np.array([a+(b-a)*t-normal*radius for t in np.linspace(.56,.85,20)])/(size-1)
 assert np.linalg.norm((forward_points(f,points)-points)*(size-1),axis=1).max()<.1,'back drift'
 motion=(forward_points(f,front[None,:])-front)*(size-1);assert np.dot(motion[0],normal)<-1,'front not contracted'
 dx=np.gradient(f,1/(w-1),axis=1);dy=np.gradient(f,1/(h-1),axis=0);assert ((1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0]).min()>.5
probe=(a+(b-a)*.60+normal*radius)/(size-1)
assert np.linalg.norm(sample(fields['body.belly'],probe[None,:]))<1e-6
assert np.linalg.norm(sample(fields['body.abdomen'],probe[None,:]))>1e-5
# Combine into the same monotone map instead of warping the already warped image.
p['settings']['parameters']={'body.abdomen':100,'body.belly':100};r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'combined.bin'),source=str(source),render=str(OUT/'combined.png'));w,h=r['meshSize'];f=np.fromfile(OUT/'combined.bin',np.float32).reshape(h,w,4)[:,:,:2];dx=np.gradient(f,1/(w-1),axis=1);dy=np.gradient(f,1/(h-1),axis=0);assert ((1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0]).min()>.5
# A controlled collapsed shoulder span exercises the explicit side anchor.
profile=copy.deepcopy(recipe);pp=profile['people'][0];pp['settings']['pose'][12][:2]=pp['settings']['pose'][11][:2]
# Preserve the torso centre in this controlled pose fixture.
for i in [11,12]:pp['settings']['pose'][i][:2]=(a/(size-1)).tolist()
call(op='portrait_geometry',size=size.tolist(),recipe=profile,source=str(source))
# Actual side-view pedestrians retain conservative rejection when the arm
# occludes the front or its hidden-side pose is too uncertain. Do not label
# the controlled shoulder test above a real-profile quality pass.
side_source=ROOT/'docs/test-evidence/M3/quality/FudanPed00001-source.png';side_recipe=json.loads((side_source.parent/'FudanPed00001-recipe.json').read_text(encoding='utf-8'));side_size=Image.open(side_source).size;refusals=[]
for index,point in [(0,[231,290]),(1,[435,314])]:
 test=copy.deepcopy(side_recipe)
 for q in test['people']:q['settings']['parameters']={}
 target=test['people'][index];target['settings']['abdomenFront']=[point[0]/side_size[0],point[1]/side_size[1]];target['settings']['parameters']={'body.belly':100}
 try:call(op='portrait_geometry',recipe=test,size=side_size,source=str(side_source));raise AssertionError('unsafe occluded side accepted')
 except RuntimeError as e:
  assert '手臂' in str(e);refusals.append(dict(person=index,reason=str(e)))
(OUT/'real-profile-limits.json').write_text(json.dumps(refusals,ensure_ascii=False,indent=2),encoding='utf-8')
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
p['settings']['parameters']={};p['settings'].pop('abdomenFront');call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=3,select=0);before=wait()['recipe']
call(op='portrait_state',parameter='body.belly',value=60);assert wait()['recipe']==before
call(op='portrait_state',click='portraitAbdomenLocate');call(op='portrait_state',paint=[.1,.1]);assert wait()['recipe']==before
call(op='portrait_state',paint=front.tolist());located=wait()['recipe'];anchor=located['people'][0]['settings']['abdomenFront'];assert np.linalg.norm((np.array(anchor)-front)*size)<3
call(op='portrait_state',parameter='body.belly',value=80);applied=wait()['recipe'];assert applied['people'][0]['settings']['parameters']['body.belly']==80
call(op='portrait_state',history=-1);assert wait()['recipe']==located;call(op='portrait_state',history=1);wait()
r=call(op='portrait_state',source=str(source),render=str(OUT/'real.png'),save=str(OUT/'belly.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_state',click='portraitAbdomenClear');cleared=wait()['recipe']['people'][0]['settings'];assert 'abdomenFront' not in cleared and cleared['parameters']['body.belly']==0,dict(anchor=cleared.get('abdomenFront'),parameters=cleared['parameters'],state={k:v for k,v in call(op='portrait_state').items() if k in ['status','panel','selected']})
call(op='portrait_state',history=-1);assert wait()['recipe']==applied
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=applied);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'real.png')),np.array(Image.open(OUT/'reopened.png')));call(op='portrait_close')
# A portable preset never transfers the source coordinate; without a target
# anchor it refuses the whole application, rather than silently losing a part.
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,incomingPreset={'parameters':{'body.belly':80}});call(op='portrait_state',panel=15);wait();before=call(op='portrait_state')['recipe'];call(op='portrait_state',presetCard=5);rejected=wait();assert rejected['recipe']==before;assert '前缘' in rejected['status'];call(op='portrait_close');call(op='shutdown');print('Front-only abdominal/local fields, fixed back, no folds, explicit location UI, clear/undo and persistence passed')
