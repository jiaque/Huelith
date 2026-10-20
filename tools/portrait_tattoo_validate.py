from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,base64,io
OUT=ROOT/'docs/test-evidence/M3/semantic-tattoo';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
real=json.loads((OUT/'hand-recipe.json').read_text(encoding='utf-8'));source=ROOT/'build/nail-evaluation/hand-1.jpg'
recipe=copy.deepcopy(real);s=recipe['people'][0]['settings'];s['tattooQuad']=[[.25,.2],[.75,.2],[.75,.8],[.25,.8]];s['parameters']={'makeup.tattoo':100};s['automaticMasks']['bodySkin']=encoded(Image.new('L',(512,512),255))
def mask(name):
 call(op='portrait_geometry',recipe=recipe,size=[512,512],styleMask='makeup.tattoo',mask=str(OUT/(name+'.png')));return np.array(Image.open(OUT/(name+'.png')))
shapes=[]
for material in ['leaf','wave','compass']:
 s['tattooMaterial']=material;a=mask(material);assert a.max()>200 and np.count_nonzero(a)>100;shapes.append(a)
assert not np.array_equal(shapes[0],shapes[1]);s['tattooMaterial']='leaf';s['parameters']['makeup.tattooCurve']=80;curved=mask('curved');assert not np.array_equal(shapes[0],curved)
s['parameters']['makeup.tattooCurve']=0;s['tattooQuad']=[[.75,.2],[.25,.8],[.25,.2],[.75,.8]];assert mask('invalid').max()==0
s['tattooQuad']=[[.25,.2],[.75,.2],[.75,.8],[.25,.8]];cloth=Image.new('L',(512,512),255);ImageDraw.Draw(cloth).rectangle((256,0,511,511),fill=0);s['automaticMasks']['bodySkin']=encoded(cloth);a=mask('cloth-protected');assert a[:,256:].max()==0 and a[:,:256].max()>200
s['automaticMasks']={};assert mask('unknown-skin').max()==0
# Actual UI against inferred body-skin on a hand-only photo, no fabricated mask.
call(op='portrait_window',path=str(source),recipe=real);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=11);before=wait()['recipe'];call(op='portrait_state',click='portraitTattoo_leaf');wait();call(op='portrait_state',paintPath=[[.38,.60],[.38,.60]]);placed=wait()['recipe'];assert len(placed['people'][0]['settings']['tattooQuad'])==4
call(op='portrait_state',history=-1);assert 'tattooQuad' not in wait()['recipe']['people'][0]['settings'];call(op='portrait_state',history=1);wait()
quad=np.array(placed['people'][0]['settings']['tattooQuad']);call(op='portrait_state',paintPath=[quad[0].tolist(),(quad[0]+[.03,-.025]).tolist()]);warped=wait()['recipe'];assert warped['people'][0]['settings']['tattooQuad']!=placed['people'][0]['settings']['tattooQuad'];call(op='portrait_state',history=-1);assert wait()['recipe']==placed;call(op='portrait_state',history=1);wait()
call(op='portrait_state',parameter='makeup.tattooCurve',value=70);call(op='portrait_state',click='portraitTattooColor_285044');colored=wait()['recipe'];r=call(op='portrait_state',source=str(source),render=str(OUT/'hand-tattoo.png'),save=str(OUT/'tattoo.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6,{'regionError':r['regionError'],'roundtrip':r['roundtrip']}
call(op='portrait_geometry',recipe=colored,size=list(Image.open(source).size),styleMask='makeup.tattoo',mask=str(OUT/'hand-mask.png'));a=np.array(Image.open(OUT/'hand-mask.png'));assert a.max()>150 and np.count_nonzero(a)>100
call(op='portrait_state',click='portraitTattooPosition');call(op='portrait_state',paintPath=[[.38,.60],[.42,.62]]);moved=wait()['recipe'];assert moved['people'][0]['settings']['tattooQuad']!=colored['people'][0]['settings']['tattooQuad'];call(op='portrait_state',history=-1);assert wait()['recipe']==colored
call(op='portrait_state',click='portraitTattooErase');call(op='portrait_state',brushSize=5,paintPath=[[.38,.60],[.39,.61]]);erased=wait()['recipe'];assert 'makeup.tattoo' in erased['people'][0]['settings']['masks'];call(op='portrait_state',history=-1);assert wait()['recipe']==colored
portable=call(op='portrait_geometry',portableSkin=colored['people'][0]['settings'],makeupPreset=True,targetSettings={})['merged'];assert portable['tattooMaterial']=='leaf' and 'tattooQuad' not in portable
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=colored);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'hand-tattoo.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Original materials, curve, folded-quad rejection, clothing/unknown protection, actual placement/corner drag, undo, erase, ROI, presets and reopen passed')
