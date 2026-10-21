"""C06 whole teeth group: automatic/manual, three treatments, protection and save."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
import copy,base64,io
OUT=ROOT/'docs/test-evidence/M3/workspace-c06';OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=json.loads((source.parent/'leroy-chiao-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
call(op='init',settings=str(OUT/'settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=13,select=0);state=wait()
 for key,value in [('teeth.yellow',60),('teeth.light',25),('teeth.natural',35)]:call(op='portrait_state',parameter=key,value=value);wait()
 call(op='portrait_state',capture=str(OUT/'085.png'));before=wait()['recipe']
 call(op='portrait_state',click='portraitTeethCorrect');state=wait();assert state['recipe']==before
 # Erase/paint a shared dental range; every treatment follows it.
 lm=state['recipe']['people'][0]['landmarks'];q=np.mean(np.array(lm)[[59,60,61,63,64,65],:2],axis=0).tolist()
 call(op='portrait_state',click='portraitTeethErase',brushSize=12,paint=q);s=wait();assert 'teeth.scope' in s['recipe']['people'][0]['settings']['masks']
 erased=s['recipe'];call(op='portrait_state',click='portraitTeethPaint',paint=q);painted=wait()['recipe'];assert painted['people'][0]['settings']['masks']!=erased['people'][0]['settings']['masks']
 call(op='portrait_state',history=-1);assert wait()['recipe']==erased
 call(op='portrait_state',history=1);assert wait()['recipe']==painted
 call(op='portrait_state',capture=str(OUT/'087.png'));call(op='portrait_state',teethMode=0);assert wait()['recipe']==painted
 call(op='portrait_state',workspaceTask=2);wait();call(op='portrait_state',workspaceTask=13);assert wait()['recipe']==painted
 r=call(op='portrait_state',source=str(source),save=str(OUT/'teeth.huelith'),render=str(OUT/'combined.png'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 call(op='portrait_close');call(op='portrait_window',project=str(OUT/'teeth.huelith'));assert wait()['recipe']==painted;call(op='portrait_close')
 # No visible teeth: only the factual status and manual entry, not a disabled slider wall.
 empty=copy.deepcopy(recipe);buf=io.BytesIO();Image.new('L',(32,32),0).save(buf,format='PNG');empty['people'][0]['settings']['automaticMasks']['teeth']=base64.b64encode(buf.getvalue()).decode();empty['people'][0]['settings']['teethStatus']='未检测到露出的牙齿'
 call(op='portrait_window',path=str(source),recipe=empty);wait();call(op='portrait_state',workspaceTask=13,select=0);wait();state=call(op='portrait_state',workspaceLayout=True,capture=str(OUT/'086.png'));assert 'portraitTeethManual' in state['visibleControls']
 call(op='portrait_state',click='portraitTeethManual');wait();manual=call(op='portrait_state',control='portraitTeethPaint',workspaceLayout=True,capture=str(OUT/'empty-manual.png'));assert manual['control']['visible'],manual;call(op='portrait_close')
 # Controlled teeth/gingiva/gap/highlight swatches isolate color from detection.
 r=copy.deepcopy(recipe);p=r['people'][0];p['landmarks']=[];p['settings']={'partialSkin':True,'parameters':{},'teethColorVersion':3,'masks':{}}
 buf=io.BytesIO();Image.new('L',(256,32),255).save(buf,format='PNG');p['settings']['masks']['teeth.scope']=base64.b64encode(buf.getvalue()).decode()
 swatches=Image.new('RGB',(256,32));d=ImageDraw.Draw(swatches)
 for i,color in enumerate([(220,210,170),(180,180,180),(160,75,85),(12,10,10),(250,250,250),(120,115,95),(200,185,170),(225,225,220)]):d.rectangle((i*32,0,i*32+31,31),fill=color)
 src=OUT/'swatches.png';swatches.save(src);frames={}
 for key,value in [('none',0),('yellow',100),('light',100),('natural',100),('natural-cool',-100)]:
  p['settings']['parameters']={} if key=='none' else {'teeth.'+key.split('-')[0]:value};dest=OUT/(key+'.bin');call(op='portrait_geometry',source=str(src),recipe=r,size=[256,32],render=str(dest.with_suffix('.png')),linearRender=str(dest));frames[key]=np.fromfile(dest,np.float32).reshape(32,256,4)
 base=frames['none'];weights=np.array([.2627,.678,.0593])
 for key,frame in frames.items():
  assert np.array_equal(frame[:,:,3],base[:,:,3]);assert np.max(abs(frame[:,64:128]-base[:,64:128]))<2e-6,(key,'gum or gap changed')
 for key in ['yellow','natural','natural-cool']:assert np.max(abs((frames[key][:,:,:3]-base[:,:,:3])@weights))<2e-6,key
 assert frames['yellow'][16,16,2]>base[16,16,2]
 assert frames['light'][16,48,:3]@weights>base[16,48,:3]@weights
 assert frames['natural'][16,48,0]>base[16,48,0]>frames['natural-cool'][16,48,0]
 assert max(a[:,:,:3].max() for a in frames.values())<=1.00001
 print('C06 automatic/empty/manual, shared coverage, independent natural color, gum/gap protection and save passed')
finally:call(op='portrait_close');call(op='shutdown')
