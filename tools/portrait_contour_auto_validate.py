from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-contour-auto';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
def decoded(value):return np.array(Image.open(io.BytesIO(base64.b64decode(value))))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text(encoding='utf-8'));recipe=copy.deepcopy(real);p=recipe['people'][0];p['face']=[.15,.1,.7,.8];s=p['settings'];s['parameters']={};s['masks']={};s['contourLightVersion']=2;s['contourAutoLighting']=True;s['automaticMasks']={'skin':encoded(Image.new('L',(512,512),255))}
source=OUT/'side-light.png';im=Image.new('RGB',(512,512),(40,40,40));ImageDraw.Draw(im).rectangle((0,0,255,511),fill=(180,180,180));im.save(source)
lighting=call(op='portrait_geometry',contourLighting=True,source=str(source),person=p);s['contourLighting']=lighting
h=decoded(lighting['highlight']);d=decoded(lighting['shadow']);assert h[100:150,60:100].mean()>240 and h[100:150,160:195].max()==0;assert d[100:150,60:100].max()==0 and d[100:150,160:195].mean()>30
Image.fromarray(h).save(OUT/'highlight-gate.png');Image.fromarray(d).save(OUT/'shadow-gate.png')
dark=OUT/'black.png';Image.new('RGB',(512,512)).save(dark);assert not call(op='portrait_geometry',contourLighting=True,source=str(dark),person=p)
unknown=copy.deepcopy(p);unknown['settings']['automaticMasks']={};assert not call(op='portrait_geometry',contourLighting=True,source=str(source),person=unknown)
portable=call(op='portrait_geometry',portableSkin={**s,'parameters':{'contour.highlight':35,'contour.shadow':25}},contourPreset=True,targetSettings={'contourLighting':{'source':'old'}})
assert portable['portable']['contourAutoLighting'] and 'contourLighting' not in portable['portable'] and 'contourLighting' not in portable['merged']
# The tonal stage must not add legacy fullness exposure a second time. Geometry remains separate.
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=copy.deepcopy(real);p=recipe['people'][0];s=p['settings'];s['masks']={};s['contourLightVersion']=2;s['contourAutoLighting']=True;s['contourLighting']=call(op='portrait_geometry',contourLighting=True,source=str(source),person=p)
def style(name,volume,high=35,shadow=25):
 s['parameters']={'face.volume':volume,'contour.highlight':high,'contour.shadow':shadow};path=OUT/(name+'.png');call(op='portrait_geometry',styleOnly=str(path),source=str(source),recipe=recipe);return np.array(Image.open(path))
a=style('combo',0);b=style('combo-volume',60);assert np.array_equal(a,b)
off0=style('off-zero',0,0,0);off1=style('off-volume',60,0,0);assert not np.array_equal(off0,off1)
s['enabled']={'contour':False};assert np.array_equal(off1,style('disabled-contour',60));s.pop('enabled')
# Actual auto action writes two visible controls, one undo, and preserves geometry values.
recipe=copy.deepcopy(real);recipe['people'][0]['settings']['parameters']={'face.volume':20}
recipe['people'][0]['settings']['enabled']={'contour':False};recipe['people'][0]['settings']['disabledParameters']={'contour.highlight':True,'contour.shadow':True}
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=13);before=wait()['recipe'];call(op='portrait_state',click='portraitContourAuto');after=wait()['recipe'];s=after['people'][0]['settings'];assert s['parameters']['face.volume']==20 and s['parameters']['contour.highlight']==35 and s['parameters']['contour.shadow']==25 and s['contourLighting']['version']==1
assert s['enabled']['contour'] and not s['disabledParameters'].get('contour.highlight') and not s['disabledParameters'].get('contour.shadow')
call(op='portrait_state',history=-1);assert wait()['recipe']==before;call(op='portrait_state',history=1);wait()
call(op='portrait_state',parameter='contour.highlight',value=50);after=wait()['recipe'];assert after['people'][0]['settings']['parameters']['contour.shadow']==25
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'auto.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,r
call(op='portrait_state',click='portraitContourAutoRelease');assert not wait()['recipe']['people'][0]['settings']['contourAutoLighting'];call(op='portrait_state',history=-1);assert wait()['recipe']==after
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'workflow.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_state',incomingPreset={**portable['portable'],'contourLighting':{'version':1,'highlight':'invalid-source-mask'}});call(op='portrait_state',select=0,panel=15);wait();call(op='portrait_state',presetCard=5);target=wait()['recipe']['people'][0]['settings'];assert target['contourLighting'].get('shadow') and target['contourLighting']['highlight']!='invalid-source-mask'
call(op='portrait_close');call(op='shutdown');print('Original-light gates, missing evidence fallback, no duplicate fullness tone, portable source isolation, actual auto action/preset, independent sliders, undo, ROI and reopening passed')
